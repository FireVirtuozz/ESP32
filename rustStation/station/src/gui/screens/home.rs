use crate::gui::ScreensTypes;

pub struct HomeScreen;

impl HomeScreen {
    pub fn show(&mut self, ctx: &egui::Context, screen: &mut ScreensTypes) {
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.vertical_centered(|ui| {
                ui.heading("RC Station");
                if ui.button("Access").clicked() {
                    *screen = ScreensTypes::Main;
                }
            });
        });
    }
}