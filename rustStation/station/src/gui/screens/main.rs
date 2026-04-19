use std::sync::{Arc, atomic::{AtomicBool, Ordering}};

use crate::gui::ScreensTypes;

pub struct MainScreen;

impl MainScreen {
    pub fn show(&mut self, ctx: &egui::Context, screen: &mut ScreensTypes,
        sensors_connected: &Arc<AtomicBool>, logs_connected: &Arc<AtomicBool>) {
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.vertical_centered(|ui| {
                ui.heading("Main menu");
                ui.add_space(20.0);
                ui.add_enabled_ui(sensors_connected.load(Ordering::Relaxed), |ui| {
                    if ui.button("Sensors").clicked() { *screen = ScreensTypes::Sensors; }
                });

                ui.add_enabled_ui(logs_connected.load(Ordering::Relaxed), |ui| {
                    if ui.button("Logs").clicked() { *screen = ScreensTypes::Logs; }
                });
                if ui.button("Commands").clicked() { *screen = ScreensTypes::Commands; }
                ui.add_space(10.0);
                if ui.button("Quit").clicked() { 
                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }
            });
        });
    }
}