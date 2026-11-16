use std::{
    collections::VecDeque,
    sync::{
        atomic::{AtomicU32, AtomicU64, Ordering},
        mpsc::{Receiver, Sender},
        Arc, RwLock,
    },
    time::{Duration, Instant},
};

use egui::{Color32, TextEdit, Ui};

use crate::{
    camera::{field_names, CameraConfig},
    gui::ScreensTypes,
    udp::udp_cfg::{CamCmd, CamEvent, SharedCamCfg},
};

const NOTICE_DURATION: Duration = Duration::from_secs(6);
const REG_LOG_LINES: usize = 12;

/// Résolutions proposées. L'ESP refuse tout ce qui dépasse le VGA (buffer JPEG alloué à l'init).
const FRAMESIZE_OPTIONS: &[(u8, &str)] = &[(1, "QQVGA 160x120"), (6, "QVGA 320x240"), (10, "VGA 640x480")];

fn framesize_label(val: u8) -> String {
    FRAMESIZE_OPTIONS
        .iter()
        .find(|(v, _)| *v == val)
        .map(|(_, l)| (*l).to_string())
        .unwrap_or_else(|| format!("#{val}"))
}

fn parse_hex(s: &str) -> Option<u32> {
    let t = s.trim();
    let t = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")).unwrap_or(t);
    u32::from_str_radix(t, 16).ok()
}

/// Brouillon édité par l'utilisateur + dernier état connu de l'ESP.
/// L'état de l'ESP reste la référence : le brouillon ne garde que ce que l'utilisateur a touché.
#[derive(Default)]
pub struct CfgDraft {
    draft: Option<CameraConfig>,
    base: Option<CameraConfig>, // dernier état de l'ESP intégré au brouillon
}

impl CfgDraft {
    /// À appeler à chaque frame avec l'état partagé. Les champs modifiés par l'utilisateur
    /// sont conservés ; les autres suivent l'ESP.
    pub fn sync(&mut self, esp: Option<CameraConfig>) {
        let Some(new) = esp else { return };
        match (self.draft, self.base) {
            (Some(d), Some(b)) => {
                if b != new {
                    let touched = d.diff(&b);
                    self.draft = Some(new.overlay(&d, touched));
                    self.base = Some(new);
                }
            }
            _ => {
                self.draft = Some(new);
                self.base = Some(new);
            }
        }
    }

    pub fn draft_mut(&mut self) -> Option<&mut CameraConfig> {
        self.draft.as_mut()
    }

    /// Champs modifiés par l'utilisateur et pas encore envoyés.
    pub fn pending(&self) -> u32 {
        match (self.draft, self.base) {
            (Some(d), Some(b)) => d.diff(&b),
            _ => 0,
        }
    }

    /// Config à envoyer. Les champs non touchés sont déjà ceux de l'ESP,
    /// donc le thread d'envoi (qui compare à l'état partagé) n'enverra que les champs touchés.
    pub fn outgoing(&self) -> Option<CameraConfig> {
        if self.pending() != 0 {
            self.draft
        } else {
            None
        }
    }

    pub fn discard(&mut self) {
        self.draft = self.base;
    }

    /// Remet certains champs à la valeur de l'ESP (par exemple ceux qu'il a refusés).
    pub fn revert_fields(&mut self, mask: u32) {
        if let (Some(d), Some(b)) = (self.draft, self.base) {
            self.draft = Some(d.overlay(&b, mask));
        }
    }
}

pub struct CameraScreen {
    pub cam_texture: Option<egui::TextureHandle>,
    last_seen_version: u64,
    cfg: CfgDraft,
    notice: Option<(String, Instant)>,
    reg_reg: String,
    reg_mask: String,
    reg_value: String,
    reg_log: VecDeque<String>,
}

impl Default for CameraScreen {
    fn default() -> Self {
        Self {
            cam_texture: None,
            last_seen_version: 0,
            cfg: CfgDraft::default(),
            notice: None,
            reg_reg: String::new(),
            reg_mask: "FF".into(),
            reg_value: String::new(),
            reg_log: VecDeque::new(),
        }
    }
}

impl CameraScreen {
    fn set_notice(&mut self, msg: String) {
        self.notice = Some((msg, Instant::now()));
    }

    fn log_reg(&mut self, line: String) {
        if self.reg_log.len() >= REG_LOG_LINES {
            self.reg_log.pop_front();
        }
        self.reg_log.push_back(line);
    }

    fn on_event(&mut self, ev: CamEvent) {
        match ev {
            CamEvent::Refused(mask) => {
                self.cfg.revert_fields(mask);
                self.set_notice(format!("Refusé par l'ESP : {}", field_names(mask).join(", ")));
            }
            CamEvent::SetTimeout => self.set_notice("Pas de réponse de l'ESP : envoi abandonné".into()),
            CamEvent::RegRead { reg, mask, value } => {
                self.log_reg(format!("R 0x{reg:04X} & 0x{mask:02X} = {value}"));
            }
            CamEvent::RegWrite { reg, mask, ret, newer } => {
                self.log_reg(format!("W 0x{reg:04X} & 0x{mask:02X} -> ret {ret}, relu {newer}"));
            }
        }
    }

    #[allow(clippy::too_many_arguments)]
    pub fn show(
        &mut self,
        ctx: &egui::Context,
        screen: &mut ScreensTypes,
        shared_texture: &Arc<RwLock<Option<egui::ColorImage>>>,
        shared_fps: &Arc<AtomicU32>,
        shared_frame_count: &Arc<AtomicU64>,
        shared_cfg: &SharedCamCfg,
        tx_cmd: &Sender<CamCmd>,
        rx_ev: &Receiver<CamEvent>,
    ) {
        // 1. État de l'ESP -> brouillon (Copy : le verrou est relâché aussitôt).
        let esp = shared_cfg.read().map(|g| *g).unwrap_or(None);
        self.cfg.sync(esp);

        // 2. Événements (champs refusés, accusés de registres, timeouts).
        while let Ok(ev) = rx_ev.try_recv() {
            self.on_event(ev);
        }

        // 3. Image + FPS.
        egui::CentralPanel::default().show(ctx, |ui| {
            if ui.button("Back").clicked() {
                *screen = ScreensTypes::Main;
            }

            let current_version = shared_frame_count.load(Ordering::Relaxed);
            if current_version != self.last_seen_version {
                if let Ok(guard) = shared_texture.read() {
                    if let Some(color_image) = guard.as_ref() {
                        match &mut self.cam_texture {
                            Some(tex) => tex.set(color_image.clone(), egui::TextureOptions::LINEAR),
                            None => {
                                self.cam_texture = Some(ctx.load_texture(
                                    "camera_frame",
                                    color_image.clone(),
                                    egui::TextureOptions::LINEAR,
                                ));
                            }
                        }
                    }
                }
                self.last_seen_version = current_version;
            }

            if let Some(texture) = &self.cam_texture {
                let response = ui.image(texture);
                let fps = shared_fps.load(Ordering::Relaxed) as f32 / 10.0;
                let painter = ui.painter_at(response.rect);
                painter.text(
                    response.rect.left_top() + egui::vec2(10.0, 10.0),
                    egui::Align2::LEFT_TOP,
                    format!("{fps:.1} FPS"),
                    egui::FontId::proportional(16.0),
                    if fps < 15.0 { Color32::RED } else { Color32::GREEN },
                );
            } else {
                ui.centered_and_justified(|ui| {
                    ui.label("Camera not connected");
                });
            }
        });

        // 4. Panneau de config.
        egui::SidePanel::right("camera_config_panel").min_width(260.0).show(ctx, |ui| {
            ui.heading("📷 Camera Config");
            ui.separator();

            if esp.is_none() {
                ui.label("En attente de la config de l'ESP…");
                if ui.button("Redemander").clicked() {
                    let _ = tx_cmd.send(CamCmd::Refresh);
                }
                return;
            }

            let pending = self.cfg.pending();

            egui::ScrollArea::vertical().max_height(ui.available_height() - 150.0).show(ui, |ui| {
                if let Some(c) = self.cfg.draft_mut() {
                    config_widgets(ui, c);
                }
            });

            ui.separator();
            if pending != 0 {
                ui.colored_label(Color32::YELLOW, format!("Non envoyé : {}", field_names(pending).join(", ")));
            }
            ui.horizontal(|ui| {
                if ui.add_enabled(pending != 0, egui::Button::new("Send config")).clicked() {
                    if let Some(c) = self.cfg.outgoing() {
                        let _ = tx_cmd.send(CamCmd::Set(c));
                    }
                }
                if ui.add_enabled(pending != 0, egui::Button::new("Discard")).clicked() {
                    self.cfg.discard();
                }
                if ui.button("Refresh").clicked() {
                    let _ = tx_cmd.send(CamCmd::Refresh);
                }
            });

            if let Some((msg, t)) = &self.notice {
                if t.elapsed() < NOTICE_DURATION {
                    ui.colored_label(Color32::LIGHT_RED, msg);
                }
            }

            ui.collapsing("Registres (debug)", |ui| {
                ui.horizontal(|ui| {
                    ui.label("reg 0x");
                    ui.add(TextEdit::singleline(&mut self.reg_reg).desired_width(48.0));
                    ui.label("mask 0x");
                    ui.add(TextEdit::singleline(&mut self.reg_mask).desired_width(30.0));
                    ui.label("val 0x");
                    ui.add(TextEdit::singleline(&mut self.reg_value).desired_width(30.0));
                });
                ui.horizontal(|ui| {
                    if ui.button("Lire").clicked() {
                        match (
                            parse_hex(&self.reg_reg).and_then(|v| u16::try_from(v).ok()),
                            parse_hex(&self.reg_mask).and_then(|v| u8::try_from(v).ok()),
                        ) {
                            (Some(reg), Some(mask)) => {
                                let _ = tx_cmd.send(CamCmd::GetReg { reg, mask });
                            }
                            _ => self.set_notice("reg / mask invalides (hex, reg ≤ FFFF, mask ≤ FF)".into()),
                        }
                    }
                    if ui.button("Écrire").clicked() {
                        match (
                            parse_hex(&self.reg_reg).and_then(|v| u16::try_from(v).ok()),
                            parse_hex(&self.reg_mask).and_then(|v| u8::try_from(v).ok()),
                            parse_hex(&self.reg_value).and_then(|v| u8::try_from(v).ok()),
                        ) {
                            (Some(reg), Some(mask), Some(value)) => {
                                let _ = tx_cmd.send(CamCmd::SetReg { reg, mask, value });
                            }
                            _ => self.set_notice("reg / mask / val invalides (hex)".into()),
                        }
                    }
                });
                for line in self.reg_log.iter().rev() {
                    ui.monospace(line);
                }
            });
        });
    }
}

fn config_widgets(ui: &mut Ui, c: &mut CameraConfig) {
    egui::ComboBox::from_label("Framesize")
        .selected_text(framesize_label(c.framesize))
        .show_ui(ui, |ui| {
            for (val, label) in FRAMESIZE_OPTIONS {
                ui.selectable_value(&mut c.framesize, *val, *label);
            }
        });
    // 6..27 MHz (datasheet OV5640). À 27 MHz, ton affichage UDP bugguait : 24 MHz est le point validé.
    ui.add(egui::Slider::new(&mut c.xclk_khz, 6000..=27000).text("XCLK (kHz)"));

    ui.separator();
    ui.label("Exposition / Gain");
    ui.checkbox(&mut c.exposure_ctrl, "Auto-exposure (AEC)");
    ui.add_enabled_ui(!c.exposure_ctrl, |ui| {
        ui.add(egui::Slider::new(&mut c.aec_value, 0..=1200).text("Exposure value"));
    });
    ui.checkbox(&mut c.aec2, "AEC2 (extended)");
    ui.add(egui::Slider::new(&mut c.ae_level, -2..=2).text("AE level"));
    ui.checkbox(&mut c.gain_ctrl, "Auto-gain (AGC)");
    ui.add_enabled_ui(!c.gain_ctrl, |ui| {
        ui.add(egui::Slider::new(&mut c.agc_gain, 0..=30).text("Gain manuel"));
    });
    ui.add(egui::Slider::new(&mut c.gainceiling, 1..=1023).text("Gain ceiling"));

    ui.separator();
    ui.label("Image");
    ui.add(egui::Slider::new(&mut c.brightness, -2..=2).text("Brightness"));
    ui.add(egui::Slider::new(&mut c.contrast, -2..=2).text("Contrast"));
    ui.add(egui::Slider::new(&mut c.saturation, -2..=2).text("Saturation"));
    ui.add(egui::Slider::new(&mut c.sharpness, -2..=2).text("Sharpness"));
    ui.add(egui::Slider::new(&mut c.denoise, 0..=8).text("Denoise"));
    ui.add(egui::Slider::new(&mut c.quality, 1..=63).text("JPEG quality"));

    ui.separator();
    ui.label("White balance");
    ui.checkbox(&mut c.whitebal, "Auto white balance");
    ui.checkbox(&mut c.awb_gain, "AWB gain");
    ui.add(egui::Slider::new(&mut c.wb_mode, 0..=4).text("WB mode"));

    ui.separator();
    ui.label("Correction");
    ui.checkbox(&mut c.dcw, "DCW");
    ui.checkbox(&mut c.bpc, "BPC");
    ui.checkbox(&mut c.wpc, "WPC");
    ui.checkbox(&mut c.raw_gma, "Raw GMA");
    ui.checkbox(&mut c.lenc, "Lens correction");

    ui.separator();
    ui.label("Divers");
    ui.checkbox(&mut c.hmirror, "H-Mirror");
    ui.checkbox(&mut c.vflip, "V-Flip");
    ui.checkbox(&mut c.colorbar, "Colorbar (test)");
    ui.add(egui::Slider::new(&mut c.special_effect, 0..=6).text("Effet"));
    // pixformat : volontairement absent. L'ESP refuse un changement à chaud.
}