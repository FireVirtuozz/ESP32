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
        Ok(Self {
            esp_id: buf[0],
            timestamp: u32::from_le_bytes([buf[1], buf[2], buf[3], buf[4]]),
            level: LogLevel::level_from_value(buf[5])?,
            tag: from_utf8(&buf[7 .. buf[6] as usize])?.to_owned(),
            msg: from_utf8(&buf[buf[6] as usize .. buf.len()])?.to_owned(),
        })
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
                ui.add_space(2.0);
                ui.separator();
                self.render_logs(ui, logs);
            });
    }

    fn render_toolbar(&mut self, ui: &mut Ui, count: usize) {
        ui.horizontal(|ui| {
            ui.label(
                RichText::new("▌LOGS")
                    .font(FontId::monospace(13.0))
                    .color(Color32::from_rgb(80, 200, 120)),
            );

            ui.label(
                RichText::new(format!("{} entries", count))
                    .font(FontId::monospace(11.0))
                    .color(Color32::from_gray(80)),
            );

            ui.add_space(8.0);

            ui.add(
                TextEdit::singleline(&mut self.search)
                    .hint_text("filter...")
                    .font(FontId::monospace(11.0))
                    .text_color(Color32::from_gray(200))
                    .desired_width(180.0),
            );

            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                let (label, color) = if self.auto_scroll {
                    ("⬇ AUTO", Color32::from_rgb(80, 200, 120))
                } else {
                    ("⬇ MANUAL", Color32::from_gray(100))
                };
                if ui.add(
                    egui::Button::new(
                        RichText::new(label).font(FontId::monospace(11.0)).color(color)
                    )
                    .fill(Color32::from_rgb(20, 24, 32))
                    .stroke(egui::Stroke::new(1.0, color)),
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

                for (i, packet) in logs.iter().enumerate() {
                    let msg = &packet.msg;

                    if !self.search.is_empty()
                        && !msg.to_lowercase().contains(&self.search.to_lowercase())
                    {
                        continue;
                    }

                    let (color, content) = (packet.level.to_color(), msg);

                    let bg = if i % 2 == 0 {
                        Color32::from_rgb(14, 16, 22)
                    } else {
                        Color32::from_rgb(10, 12, 16)
                    };

                    egui::Frame::none()
                        .fill(bg)
                        .inner_margin(egui::Margin::symmetric(6.0, 2.0))
                        .show(ui, |ui| {
                            ui.set_min_width(ui.available_width());
                            ui.horizontal(|ui| {
                                // Numéro de ligne
                                ui.label(
                                    RichText::new(format!("{:>4}", i + 1))
                                        .font(FontId::monospace(11.0))
                                        .color(Color32::from_gray(40)),
                                );

                                ui.label(
                                    RichText::new("│")
                                        .font(FontId::monospace(11.0))
                                        .color(Color32::from_gray(30)),
                                );

                                // Couleur du level en carré
                                let (_, level_str) = if msg.starts_with('[') {
                                    let end = msg.find(']').unwrap_or(0);
                                    (color, &msg[..=end])
                                } else {
                                    (color, "")
                                };

                                if !level_str.is_empty() {
                                    ui.label(
                                        RichText::new(level_str)
                                            .font(FontId::monospace(11.0))
                                            .color(color),
                                    );
                                }

                                // Message
                                ui.label(
                                    RichText::new(content)
                                        .font(FontId::monospace(11.0))
                                        .color(Color32::from_gray(200)),
                                );
                            });
                        });
                }
            });
    }
}