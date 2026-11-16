use std::sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc};

use crate::{controller::ControllerPacket, gui::ScreensTypes};

pub struct CommandsScreen {
    last_packet: Option<ControllerPacket>,
}

impl Default for CommandsScreen {
    fn default() -> Self {
        Self {
            last_packet: Some(ControllerPacket::default()),
        }
    }
}

impl CommandsScreen {
    pub fn show(&mut self, ctx: &egui::Context, controller_connected: &Arc<AtomicBool>,
        rx_ctrl: &mpsc::Receiver<ControllerPacket>, screen: &mut ScreensTypes) {

        while let Ok(pkt) = rx_ctrl.try_recv() {
            self.last_packet = Some(pkt);
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            let connected = controller_connected.load(Ordering::Relaxed);
            
            // --- status bar ---
            ui.horizontal(|ui| {
                let (color, text) = if connected {
                    (egui::Color32::from_rgb(29, 158, 117), "connected")
                } else {
                    (egui::Color32::GRAY, "disconnected")
                };
                ui.colored_label(color, "●");
                ui.label(text);
            });
            ui.separator();

            if connected {
                if let Some(pkt) = &self.last_packet {
                    ui.columns(3, |cols| {
                        // left stick
                        cols[0].label("left stick");
                        draw_stick(&mut cols[0], pkt.left_x, pkt.left_y);
                        cols[0].label(format!("x: {}  y: {}", pkt.left_x, pkt.left_y));

                        // right stick
                        cols[1].label("right stick");
                        draw_stick(&mut cols[1], pkt.right_x, pkt.right_y);
                        cols[1].label(format!("x: {}  y: {}", pkt.right_x, pkt.right_y));

                        // triggers
                        cols[2].label("L trigger");
                        let lt_pct = pkt.left_trig;
                        cols[2].add(egui::ProgressBar::new(lt_pct).text(format!("{}", pkt.left_trig)));

                        cols[2].add_space(8.0);
                        cols[2].label("R trigger");
                        let rt_pct = pkt.right_trig;
                        cols[2].add(egui::ProgressBar::new(rt_pct).text(format!("{}", pkt.right_trig)));
                    });

                    ui.separator();

                    // buttons mask
                    ui.label("buttons");
                    ui.horizontal_wrapped(|ui| {
                        let names = [
                            ("A", pkt.down_button),
                            ("B", pkt.right_button),
                            ("X", pkt.left_button),
                            ("Y", pkt.up_button),
                            ("Down", pkt.down_arrow),
                            ("Left", pkt.left_arrow),
                            ("Right", pkt.right_arrow),
                            ("Up", pkt.up_arrow)
                        ];
                        for (name, value) in names {
                            let (bg, fg) = if value {
                                (egui::Color32::from_rgb(24, 95, 165), egui::Color32::WHITE)
                            } else {
                                (egui::Color32::from_gray(40), egui::Color32::GRAY)
                            };
                            egui::Frame::new()
                                .fill(bg)
                                .corner_radius(6.0)
                                .inner_margin(egui::Margin::symmetric(10, 4))
                                .show(ui, |ui| {
                                    ui.colored_label(fg, name);
                            });
                        };
                    });
                } 
            } else {
                ui.centered_and_justified(|ui| {
                    ui.label("Please connect your controller");
                });
            }
            ui.with_layout(egui::Layout::bottom_up(egui::Align::LEFT), |ui| {
                if ui.button("Back").clicked() {
                    *screen = ScreensTypes::Main;
                }
            });

            
        });
    }
}

fn draw_stick(ui: &mut egui::Ui, x: f32, y: f32) {
    let size = egui::vec2(80.0, 80.0);
    let (rect, _) = ui.allocate_exact_size(size, egui::Sense::hover());
    
    if ui.is_rect_visible(rect) {
        let painter = ui.painter();
        let center = rect.center();
        let radius = rect.width() / 2.0 - 4.0;
        
        // circle background + cross
        painter.circle_stroke(center, radius, egui::Stroke::new(1.0, egui::Color32::DARK_GRAY));
        painter.line_segment([center - egui::vec2(radius, 0.0), center + egui::vec2(radius, 0.0)],
            egui::Stroke::new(0.5, egui::Color32::DARK_GRAY));
        painter.line_segment([center - egui::vec2(0.0, radius), center + egui::vec2(0.0, radius)],
            egui::Stroke::new(0.5, egui::Color32::DARK_GRAY));
        
        // position point
        let nx = x * radius;
        let ny = y * radius;
        let dot = center + egui::vec2(nx, ny);
        painter.line_segment([center, dot], egui::Stroke::new(1.5, egui::Color32::from_rgb(55, 138, 221)));
        painter.circle_filled(dot, 5.0, egui::Color32::from_rgb(55, 138, 221));
    }
}