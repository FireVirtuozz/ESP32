use std::{collections::VecDeque, str::from_utf8};
use egui::{Color32, FontId, RichText, ScrollArea, TextEdit, Ui};

use crate::error::AppError;

pub enum LogLevel {
    VERBOSE = 5,
    DEBUG = 4,
    INFO = 3,
    WARN = 2,
    ERROR = 1,
}

impl LogLevel {
    fn to_color(&self) -> Color32 {
        match self {
            LogLevel::VERBOSE => Color32::from_gray(180),
            LogLevel::DEBUG => Color32::from_rgb(160, 120, 255),
            LogLevel::INFO => Color32::from_rgb(100, 180, 255),
            LogLevel::WARN => Color32::from_rgb(255, 200, 60),
            LogLevel::ERROR => Color32::from_rgb(255, 80, 80),
        }
    }

    fn level_from_value(val: u8) -> Result<Self, AppError> {
        match val {
            5 => Ok(Self::VERBOSE),
            4 => Ok(Self::DEBUG),
            3 => Ok(Self::INFO),
            2 => Ok(Self::WARN),
            1 => Ok(Self::ERROR),
            _ => Err("Invalid value for LogLevel".into()),
        }
    }

    fn as_str(&self) -> &'static str {
        match self {
            LogLevel::VERBOSE => "VRB",
            LogLevel::DEBUG   => "DBG",
            LogLevel::INFO    => "INF",
            LogLevel::WARN    => "WRN",
            LogLevel::ERROR   => "ERR",
        }
    }
}

pub struct LogPacket {
    pub timestamp : u32,
    pub tag: String,
    pub esp_id: u8,
    pub level: LogLevel,
    pub msg: String,
}

impl LogPacket {

    pub fn log_from_buffer(buf: &[u8]) -> Result<Self, AppError> {
        let tag_len = buf[6];
        Ok(
            Self {
            esp_id: buf[0],
            timestamp: u32::from_le_bytes([buf[1], buf[2], buf[3], buf[4]]),
            level: LogLevel::level_from_value(buf[5])?,
            tag: from_utf8(&buf[7 .. 7 + tag_len as usize])?.to_owned(),
            msg: from_utf8(&buf[7 + tag_len as usize .. buf.len()])?.to_owned(),
        })
    }

    pub fn formatted_time(&self) -> String {
        let total_secs = self.timestamp / 1000;
        let mins = total_secs / 60;
        let secs = total_secs % 60;
        let ms = self.timestamp % 1000;
        format!("[{:02}:{:02}.{:03}]", mins, secs, ms)
    }
}

pub struct LogsScreen {
    pub search: String,
    pub auto_scroll: bool,
}

impl Default for LogsScreen {
    fn default() -> Self {
        Self {
            search: String::new(),
            auto_scroll: true,
        }
    }
}

impl LogsScreen {
    pub fn show(&mut self, ctx: &egui::Context, logs: &VecDeque<LogPacket>) {
        egui::CentralPanel::default()
            .frame(egui::Frame::none().fill(Color32::from_rgb(10, 12, 16)))
            .show(ctx, |ui| {
                self.render_toolbar(ui, logs.len());
                ui.add_space(4.0);
                ui.separator();
                ui.add_space(2.0);
                self.render_logs(ui, logs);
            });
    }

    fn render_toolbar(&mut self, ui: &mut Ui, count: usize) {
        ui.horizontal(|ui| {
            ui.label(
                RichText::new("▌ SYSTEM LOGS")
                    .font(FontId::monospace(13.0))
                    .color(Color32::from_rgb(80, 200, 120)),
            );

            ui.label(
                RichText::new(format!("({} entries)", count))
                    .font(FontId::monospace(11.0))
                    .color(Color32::from_gray(100)),
            );

            ui.add_space(12.0);

            // Filtre de recherche global
            ui.add(
                TextEdit::singleline(&mut self.search)
                    .hint_text("🔍 Filter by msg, tag, level...")
                    .font(FontId::monospace(11.0))
                    .text_color(Color32::from_gray(200))
                    .desired_width(220.0),
            );

            // Bouton auto-scroll aligné à droite
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                let (label, color) = if self.auto_scroll {
                    ("⬇ AUTO SCROLL", Color32::from_rgb(80, 200, 120))
                } else {
                    ("⏸ MANUAL", Color32::from_gray(120))
                };
                
                if ui.add(
                    egui::Button::new(
                        RichText::new(label).font(FontId::new(10.0, egui::FontFamily::Monospace)).color(color)
                    )
                    .fill(Color32::from_rgb(18, 22, 30))
                    .stroke(egui::Stroke::new(1.0, color.linear_multiply(0.5))),
                ).clicked() {
                    self.auto_scroll = !self.auto_scroll;
                }
            });
        });
    }

    fn render_logs(&mut self, ui: &mut Ui, logs: &VecDeque<LogPacket>) {
        ScrollArea::vertical()
            .auto_shrink([false, false])
            .stick_to_bottom(self.auto_scroll)
            .show(ui, |ui| {
                ui.set_min_width(ui.available_width());

                let search_lower = self.search.to_lowercase();

                for (i, packet) in logs.iter().enumerate() {
                    // Match de recherche intelligent (Cherche dans le message, le tag ou le nom du level)
                    if !self.search.is_empty() 
                        && !packet.msg.to_lowercase().contains(&search_lower)
                        && !packet.tag.to_lowercase().contains(&search_lower)
                        && !packet.level.as_str().to_lowercase().contains(&search_lower)
                    {
                        continue;
                    }

                    // Alternance background
                    let bg = if i % 2 == 0 {
                        Color32::from_rgb(14, 16, 22)
                    } else {
                        Color32::from_rgb(8, 10, 14)
                    };

                    egui::Frame::none()
                        .fill(bg)
                        .inner_margin(egui::Margin::symmetric(8.0, 3.0))
                        .show(ui, |ui| {
                            ui.set_min_width(ui.available_width());
                            ui.horizontal(|ui| {
                                
                                // 1. Index de ligne
                                ui.label(
                                    RichText::new(format!("{:>4}", i + 1))
                                        .font(FontId::monospace(10.0))
                                        .color(Color32::from_gray(50)),
                                );

                                ui.label(
                                    RichText::new(format!("[ESP #{}]", packet.esp_id))
                                        .font(FontId::monospace(11.0))
                                        .color(Color32::from_rgb(130, 140, 150)),
                                );

                                // 2. Timestamp formaté de l'ESP32 [MM:SS.mmm]
                                ui.label(
                                    RichText::new(packet.formatted_time())
                                        .font(FontId::monospace(11.0))
                                        .color(Color32::from_rgb(130, 140, 150)),
                                );

                                // 3. Badge du niveau de log [INF], [WRN], etc.
                                let lvl_color = packet.level.to_color();
                                ui.label(
                                    RichText::new(format!("[{}]", packet.level.as_str()))
                                        .font(FontId::new(11.0, egui::FontFamily::Monospace))
                                        .color(lvl_color),
                                );

                                // 4. Le Tag (ex: CAMERA, WIFI) en jaune/orange discret pour bien le dissocier
                                ui.label(
                                    RichText::new(format!("{}:", packet.tag))
                                        .font(FontId::new(11.0, egui::FontFamily::Monospace))
                                        .color(Color32::from_rgb(220, 160, 100)),
                                );

                                // 5. Le message de log textuel
                                ui.label(
                                    RichText::new(&packet.msg)
                                        .font(FontId::monospace(11.0))
                                        .color(Color32::from_gray(210)),
                                );
                            });
                        });
                }
            });
    }
}