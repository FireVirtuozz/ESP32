use std::{collections::VecDeque, net::UdpSocket, time::Instant};
use egui::{Color32, FontId, RichText, ScrollArea, TextEdit, Ui, epaint::image};
use ::image::load_from_memory;
use log::{debug, error, warn};

use crate::config::{AppConfig, CamFormat};

pub const CAM_CFG_FRAMESIZE: u32      = 1 << 0;
pub const CAM_CFG_BRIGHTNESS: u32     = 1 << 1;
pub const CAM_CFG_CONTRAST: u32       = 1 << 2;
pub const CAM_CFG_SATURATION: u32     = 1 << 3;
pub const CAM_CFG_SHARPNESS: u32      = 1 << 4;
pub const CAM_CFG_DENOISE: u32        = 1 << 5;
pub const CAM_CFG_QUALITY: u32        = 1 << 6;
pub const CAM_CFG_GAINCEILING: u32    = 1 << 7;
pub const CAM_CFG_COLORBAR: u32       = 1 << 8;
pub const CAM_CFG_WHITEBAL: u32       = 1 << 9;
pub const CAM_CFG_AWB_GAIN: u32       = 1 << 10;
pub const CAM_CFG_WB_MODE: u32        = 1 << 11;
pub const CAM_CFG_EXPOSURE_CTRL: u32  = 1 << 12;
pub const CAM_CFG_AEC2: u32           = 1 << 13;
pub const CAM_CFG_AE_LEVEL: u32       = 1 << 14;
pub const CAM_CFG_AEC_VALUE: u32      = 1 << 15;
pub const CAM_CFG_GAIN_CTRL: u32      = 1 << 16;
pub const CAM_CFG_AGC_GAIN: u32       = 1 << 17;
pub const CAM_CFG_HMIRROR: u32        = 1 << 18;
pub const CAM_CFG_VFLIP: u32          = 1 << 19;
pub const CAM_CFG_DCW: u32            = 1 << 20;
pub const CAM_CFG_BPC: u32            = 1 << 21;
pub const CAM_CFG_WPC: u32            = 1 << 22;
pub const CAM_CFG_RAW_GMA: u32        = 1 << 23;
pub const CAM_CFG_LENC: u32           = 1 << 24;
pub const CAM_CFG_SPECIAL_EFFECT: u32 = 1 << 25;

#[derive(Default, Clone, Copy, Debug)]
pub struct CameraConfig {
    pub framesize: Option<u8>,
    pub brightness: Option<i8>,
    pub contrast: Option<i8>,
    pub saturation: Option<i8>,
    pub sharpness: Option<i8>,
    pub denoise: Option<u8>,
    pub quality: Option<u8>,
    pub gainceiling: Option<u8>,
    pub colorbar: Option<bool>,
    pub whitebal: Option<bool>,
    pub awb_gain: Option<bool>,
    pub wb_mode: Option<u8>,
    pub exposure_ctrl: Option<bool>,
    pub aec2: Option<bool>,
    pub ae_level: Option<i8>,
    pub aec_value: Option<u16>,
    pub gain_ctrl: Option<bool>,
    pub agc_gain: Option<u8>,
    pub hmirror: Option<bool>,
    pub vflip: Option<bool>,
    pub dcw: Option<bool>,
    pub bpc: Option<bool>,
    pub wpc: Option<bool>,
    pub raw_gma: Option<bool>,
    pub lenc: Option<bool>,
    pub special_effect: Option<u8>,
}

impl CameraConfig {
    pub fn to_bytes(&self) -> [u8; 31] {
        let mut frame = [0u8; 31];
        let mut mask: u32 = 0;

        if let Some(v) = self.framesize      { mask |= CAM_CFG_FRAMESIZE;      frame[4] = v; }
        if let Some(v) = self.brightness     { mask |= CAM_CFG_BRIGHTNESS;     frame[5] = v as u8; }
        if let Some(v) = self.contrast       { mask |= CAM_CFG_CONTRAST;       frame[6] = v as u8; }
        if let Some(v) = self.saturation     { mask |= CAM_CFG_SATURATION;     frame[7] = v as u8; }
        if let Some(v) = self.sharpness      { mask |= CAM_CFG_SHARPNESS;      frame[8] = v as u8; }
        if let Some(v) = self.denoise        { mask |= CAM_CFG_DENOISE;        frame[9] = v; }
        if let Some(v) = self.quality        { mask |= CAM_CFG_QUALITY;        frame[10] = v; }
        if let Some(v) = self.gainceiling    { mask |= CAM_CFG_GAINCEILING;    frame[11] = v; }
        if let Some(v) = self.colorbar       { mask |= CAM_CFG_COLORBAR;       frame[12] = v as u8; }
        if let Some(v) = self.whitebal       { mask |= CAM_CFG_WHITEBAL;       frame[13] = v as u8; }
        if let Some(v) = self.awb_gain       { mask |= CAM_CFG_AWB_GAIN;       frame[14] = v as u8; }
        if let Some(v) = self.wb_mode        { mask |= CAM_CFG_WB_MODE;        frame[15] = v; }
        if let Some(v) = self.exposure_ctrl  { mask |= CAM_CFG_EXPOSURE_CTRL;  frame[16] = v as u8; }
        if let Some(v) = self.aec2           { mask |= CAM_CFG_AEC2;           frame[17] = v as u8; }
        if let Some(v) = self.ae_level       { mask |= CAM_CFG_AE_LEVEL;       frame[18] = v as u8; }
        if let Some(v) = self.aec_value      { mask |= CAM_CFG_AEC_VALUE;      frame[19..21].copy_from_slice(&v.to_le_bytes()); }
        if let Some(v) = self.gain_ctrl      { mask |= CAM_CFG_GAIN_CTRL;      frame[21] = v as u8; }
        if let Some(v) = self.agc_gain       { mask |= CAM_CFG_AGC_GAIN;       frame[22] = v; }
        if let Some(v) = self.hmirror        { mask |= CAM_CFG_HMIRROR;        frame[23] = v as u8; }
        if let Some(v) = self.vflip          { mask |= CAM_CFG_VFLIP;          frame[24] = v as u8; }
        if let Some(v) = self.dcw            { mask |= CAM_CFG_DCW;            frame[25] = v as u8; }
        if let Some(v) = self.bpc            { mask |= CAM_CFG_BPC;            frame[26] = v as u8; }
        if let Some(v) = self.wpc            { mask |= CAM_CFG_WPC;            frame[27] = v as u8; }
        if let Some(v) = self.raw_gma        { mask |= CAM_CFG_RAW_GMA;        frame[28] = v as u8; }
        if let Some(v) = self.lenc           { mask |= CAM_CFG_LENC;           frame[29] = v as u8; }
        if let Some(v) = self.special_effect { mask |= CAM_CFG_SPECIAL_EFFECT; frame[30] = v; }

        frame[0..4].copy_from_slice(&mask.to_le_bytes());
        frame
    }
}

#[derive(Clone, Copy)]
pub struct CameraConfigUi {
    pub framesize: u8,
    pub brightness: i8,
    pub contrast: i8,
    pub saturation: i8,
    pub sharpness: i8,
    pub denoise: u8,
    pub quality: u8,
    pub gainceiling: u8,
    pub colorbar: bool,
    pub whitebal: bool,
    pub awb_gain: bool,
    pub wb_mode: u8,
    pub exposure_ctrl: bool,
    pub aec2: bool,
    pub ae_level: i8,
    pub aec_value: u16,
    pub gain_ctrl: bool,
    pub agc_gain: u8,
    pub hmirror: bool,
    pub vflip: bool,
    pub dcw: bool,
    pub bpc: bool,
    pub wpc: bool,
    pub raw_gma: bool,
    pub lenc: bool,
    pub special_effect: u8,
}

impl Default for CameraConfigUi {
    fn default() -> Self {
        Self {
            framesize: 10, // QVGA -- vérifie l'ordre exact de framesize_t dans TON sensor.h
            brightness: 0, contrast: 0, saturation: 0, sharpness: 0,
            denoise: 0, quality: 12, gainceiling: 0,
            colorbar: false, whitebal: true, awb_gain: true, wb_mode: 0,
            exposure_ctrl: true, aec2: false, ae_level: 0, aec_value: 300,
            gain_ctrl: true, agc_gain: 0,
            hmirror: false, vflip: false, dcw: true, bpc: false, wpc: false,
            raw_gma: true, lenc: true, special_effect: 0,
        }
    }
}

impl CameraConfigUi {
    // Envoie TOUS les champs à chaque appel -> état ESP toujours cohérent, pas de tracking de "dirty fields"
    pub fn to_full_config(&self) -> CameraConfig {
        CameraConfig {
            framesize: Some(self.framesize), brightness: Some(self.brightness),
            contrast: Some(self.contrast), saturation: Some(self.saturation),
            sharpness: Some(self.sharpness), denoise: Some(self.denoise),
            quality: Some(self.quality), gainceiling: Some(self.gainceiling),
            colorbar: Some(self.colorbar), whitebal: Some(self.whitebal),
            awb_gain: Some(self.awb_gain), wb_mode: Some(self.wb_mode),
            exposure_ctrl: Some(self.exposure_ctrl), aec2: Some(self.aec2),
            ae_level: Some(self.ae_level), aec_value: Some(self.aec_value),
            gain_ctrl: Some(self.gain_ctrl), agc_gain: Some(self.agc_gain),
            hmirror: Some(self.hmirror), vflip: Some(self.vflip),
            dcw: Some(self.dcw), bpc: Some(self.bpc), wpc: Some(self.wpc),
            raw_gma: Some(self.raw_gma), lenc: Some(self.lenc),
            special_effect: Some(self.special_effect),
        }
    }
}

pub struct CameraScreen {
    texture: Option<egui::TextureHandle>,
    pub cfg: CameraConfigUi,
    pub socket_udp_cam: UdpSocket,
    last_fps_time: std::time::Instant,
    frame_count: u32,
    fps: f32,
}

fn yuv422_to_rgba(yuv: &[u8], width: usize, height: usize) -> Vec<u8> {
    let mut rgba = Vec::with_capacity(width * height * 4);

    //2 pix at a time
    for chunk in yuv.chunks_exact(4) {
        let y0 = chunk[0] as f32;
        let u  = chunk[1] as f32 - 128.0;
        let y1 = chunk[2] as f32;
        let v  = chunk[3] as f32 - 128.0;

        // Conversion formula YUV -> RGB
        let r_offset = 1.402 * v;
        let g_offset = -0.344136 * u - 0.714136 * v;
        let b_offset = 1.772 * u;

        // Pixel 1
        rgba.push((y0 + r_offset).clamp(0.0, 255.0) as u8); // R
        rgba.push((y0 + g_offset).clamp(0.0, 255.0) as u8); // G
        rgba.push((y0 + b_offset).clamp(0.0, 255.0) as u8); // B
        rgba.push(255);                                     // A (Opale)

        // Pixel 2
        rgba.push((y1 + r_offset).clamp(0.0, 255.0) as u8); // R
        rgba.push((y1 + g_offset).clamp(0.0, 255.0) as u8); // G
        rgba.push((y1 + b_offset).clamp(0.0, 255.0) as u8); // B
        rgba.push(255);                                     // A
    }

    rgba
}

impl Default for CameraScreen {
    fn default() -> Self {
        Self {
            texture: None,
            cfg : CameraConfigUi::default(),
            socket_udp_cam: UdpSocket::bind("0.0.0.0:0").unwrap(),
            fps: 0.0,
            frame_count: 0,
            last_fps_time: Instant::now(),
        }
    }
}


impl CameraScreen {
    pub fn show(&mut self, ctx: &egui::Context, frame: &mut Option<Vec<u8>>, config_egui: &AppConfig) {
        let mut new_frame_decoded = false; // Flag pour savoir si on a reçu une frame valide

        if let Some(binary_data) = frame.take() {

            match config_egui.cam_format {
                CamFormat::JPEG => {
                        debug!("image received ({}) header: {:02X}{:02X} footer: {:02X}{:02X}", 
                        binary_data.len(), 
                        binary_data[0], binary_data[1],
                        binary_data[binary_data.len()-2], binary_data[binary_data.len()-1]
                    );

                    let _ = std::fs::write("debug_camera.jpg", &binary_data);
                    
                    if let Ok(img) = load_from_memory(&binary_data) {
                        let rgba = img.to_rgba8();
                        let pixels = rgba.as_flat_samples();
                        
                        // Convert to native egui format
                        let color_image = egui::ColorImage::from_rgba_unmultiplied(
                            [img.width() as usize, img.height() as usize],
                            pixels.as_slice(),
                        );

                        if let Some(tex) = &mut self.texture {
                            tex.set(color_image, Default::default());  // update sans recréer
                        } else {
                            self.texture = Some(ctx.load_texture(
                                "camera_stream",
                                color_image,
                                Default::default()
                            ));
                        }

                        new_frame_decoded = true;
                        ctx.request_repaint();
                    } else {
                        error!("failed to load image");
                    }
                }, 
                CamFormat::YUV => {
                    let (cam_width, cam_height) = config_egui.cam_res.dimensions();
                    let expected_len = cam_width * cam_height * 2;

                    if binary_data.len() == expected_len {
                        // Converts yuv to rgba
                        let rgba_pixels = yuv422_to_rgba(&binary_data, cam_width, cam_height);
                        
                        // RGBA to egui format
                        let color_image = egui::ColorImage::from_rgba_unmultiplied(
                            [cam_width, cam_height],
                            &rgba_pixels,
                        );

                        if let Some(tex) = &mut self.texture {
                            tex.set(color_image, Default::default());
                        } else {
                            self.texture = Some(ctx.load_texture(
                                "camera_stream",
                                color_image,
                                Default::default()
                            ));
                        }

                        new_frame_decoded = true;
                        ctx.request_repaint();
                    } else {
                        error!(
                            "Wrong size YUV ! Recv: {}, Expected: {}", 
                            binary_data.len(), 
                            expected_len
                        );
                    }
                },
                CamFormat::RGB => {
                    warn!("RGB not implemented");
                },
            }
        }

        // --- CALCUL DES FPS ---
        if new_frame_decoded {
            self.frame_count += 1;
            let elapsed = self.last_fps_time.elapsed();
            
            // On met à jour le calcul toutes les secondes pour éviter que ça clignote trop vite
            if elapsed.as_secs_f32() >= 1.0 {
                self.fps = self.frame_count as f32 / elapsed.as_secs_f32();
                self.frame_count = 0;
                self.last_fps_time = std::time::Instant::now();
            }
        }
            

        egui::CentralPanel::default().show(ctx, |ui| {
            if let Some(texture) = &self.texture {
                let img = egui::Image::from_texture(texture)
                    .max_size(ui.available_size())
                    .maintain_aspect_ratio(true);

                let response = ui.add(img);

                // On dessine le texte des FPS par-dessus l'image
                let painter = ui.painter_at(response.rect);
                painter.text(
                    response.rect.left_top() + egui::vec2(10.0, 10.0), // Position (10px du bord haut/gauche)
                    egui::Align2::LEFT_TOP,
                    format!("{:.1} FPS", self.fps),
                    egui::FontId::proportional(16.0),
                    if self.fps < 15.0 { egui::Color32::RED } else { egui::Color32::GREEN } // Rouge si ça rame, Vert si c'est fluide
                );
            } else {
                ui.centered_and_justified(|ui| {
                    ui.label("Camera not connected");
                });
            }
        });

        let mut changed = false;

        egui::SidePanel::right("camera_config_panel").min_width(260.0).show(ctx, |ui| {
            ui.heading("📷 Camera Config");
            ui.separator();

            egui::ComboBox::from_label("Framesize")
                .selected_text(framesize_label(self.cfg.framesize))
                .show_ui(ui, |ui| {
                    for (val, label) in FRAMESIZE_OPTIONS {
                        changed |= ui.selectable_value(&mut self.cfg.framesize, *val, *label).changed();
                    }
                });

            ui.separator();
            ui.label("Exposition / Gain");
            changed |= ui.checkbox(&mut self.cfg.exposure_ctrl, "Auto-exposure (AEC)").changed();
            ui.add_enabled_ui(!self.cfg.exposure_ctrl, |ui| {
                changed |= ui.add(egui::Slider::new(&mut self.cfg.aec_value, 0..=1200).text("Exposure value")).changed();
            });
            changed |= ui.checkbox(&mut self.cfg.aec2, "AEC2 (extended)").changed();
            changed |= ui.add(egui::Slider::new(&mut self.cfg.ae_level, -2..=2).text("AE level")).changed();

            changed |= ui.checkbox(&mut self.cfg.gain_ctrl, "Auto-gain (AGC)").changed();
            ui.add_enabled_ui(!self.cfg.gain_ctrl, |ui| {
                changed |= ui.add(egui::Slider::new(&mut self.cfg.agc_gain, 0..=30).text("Gain manuel")).changed();
            });
            changed |= ui.add(egui::Slider::new(&mut self.cfg.gainceiling, 0..=6).text("Gain ceiling")).changed();

            ui.separator();
            ui.label("Image");
            changed |= ui.add(egui::Slider::new(&mut self.cfg.brightness, -2..=2).text("Brightness")).changed();
            changed |= ui.add(egui::Slider::new(&mut self.cfg.contrast, -2..=2).text("Contrast")).changed();
            changed |= ui.add(egui::Slider::new(&mut self.cfg.saturation, -2..=2).text("Saturation")).changed();
            changed |= ui.add(egui::Slider::new(&mut self.cfg.sharpness, -2..=2).text("Sharpness")).changed();
            changed |= ui.add(egui::Slider::new(&mut self.cfg.denoise, 0..=8).text("Denoise")).changed();
            changed |= ui.add(egui::Slider::new(&mut self.cfg.quality, 0..=63).text("JPEG quality")).changed();

            ui.separator();
            ui.label("Balance des blancs");
            changed |= ui.checkbox(&mut self.cfg.whitebal, "Auto white balance").changed();
            changed |= ui.checkbox(&mut self.cfg.awb_gain, "AWB gain").changed();
            changed |= ui.add(egui::Slider::new(&mut self.cfg.wb_mode, 0..=4).text("WB mode")).changed();

            ui.separator();
            ui.label("Correction");
            changed |= ui.checkbox(&mut self.cfg.dcw, "DCW").changed();
            changed |= ui.checkbox(&mut self.cfg.bpc, "BPC").changed();
            changed |= ui.checkbox(&mut self.cfg.wpc, "WPC").changed();
            changed |= ui.checkbox(&mut self.cfg.raw_gma, "Raw GMA").changed();
            changed |= ui.checkbox(&mut self.cfg.lenc, "Lens correction").changed();

            ui.separator();
            ui.label("Divers");
            changed |= ui.checkbox(&mut self.cfg.hmirror, "H-Mirror").changed();
            changed |= ui.checkbox(&mut self.cfg.vflip, "V-Flip").changed();
            changed |= ui.checkbox(&mut self.cfg.colorbar, "Colorbar (test)").changed();
            changed |= ui.add(egui::Slider::new(&mut self.cfg.special_effect, 0..=6).text("Effet")).changed();

            ui.separator();
            if ui.button("↺ Reset défaut").clicked() {
                self.cfg = CameraConfigUi::default();
                changed = true;
            }
        });

        if changed { 
            self.socket_udp_cam.send_to(&self.cfg.to_full_config().to_bytes(), "192.168.1.59:3334")
                .expect("couldn't bind to address");
            println!("send cam: {:?}", &self.cfg.to_full_config());
        }
        
    }
}

const FRAMESIZE_OPTIONS: &[(u8, &str)] = &[
    (6, "QVGA 320x240"), (10, "VGA 640x480"), (11, "SVGA 800x600"), (15, "UXGA 1600x1200"),
];

fn framesize_label(val: u8) -> &'static str {
    FRAMESIZE_OPTIONS.iter().find(|(v, _)| *v == val).map(|(_, l)| *l).unwrap_or("?")
}