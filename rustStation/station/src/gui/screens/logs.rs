use std::collections::VecDeque;
use egui::{Color32, FontId, RichText, ScrollArea, TextEdit, Ui};
use crate::monitor::LogPacket;

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

fn parse_level(msg: &str) -> (Color32, &str) {
    let msg = msg.trim();
    if let Some(rest) = msg.strip_prefix("[ERROR]") {
        (Color32::from_rgb(255, 80, 80), rest.trim())
    } else if let Some(rest) = msg.strip_prefix("[WARN]") {
        (Color32::from_rgb(255, 200, 60), rest.trim())
    } else if let Some(rest) = msg.strip_prefix("[DEBUG]") {
        (Color32::from_rgb(160, 120, 255), rest.trim())
    } else if let Some(rest) = msg.strip_prefix("[INFO]") {
        (Color32::from_rgb(100, 180, 255), rest.trim())
    } else {
        (Color32::from_gray(180), msg)
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
                    let msg = match &packet.msg {
                        Some(m) => m.as_str(),
                        None => continue,
                    };

                    if !self.search.is_empty()
                        && !msg.to_lowercase().contains(&self.search.to_lowercase())
                    {
                        continue;
                    }

                    let (color, content) = parse_level(msg);

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