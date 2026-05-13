use std::collections::VecDeque;
use egui::{Color32, FontId, RichText, ScrollArea, TextEdit, Ui, epaint::image};
use ::image::load_from_memory;

pub struct CameraScreen {
    texture: Option<egui::TextureHandle>,
}

impl Default for CameraScreen {
    fn default() -> Self {
        Self {
            texture: None,
        }
    }
}

impl CameraScreen {
    pub fn show(&mut self, ctx: &egui::Context, frame: &mut Option<Vec<u8>>) {
        if let Some(binary_data) = frame.take() {
            if let Ok(img) = load_from_memory(&binary_data) {
                let rgba = img.to_rgba8();
                let pixels = rgba.as_flat_samples();
                
                // Convert to native egui format
                let color_image = egui::ColorImage::from_rgba_unmultiplied(
                    [img.width() as usize, img.height() as usize],
                    pixels.as_slice(),
                );

                // Update texture
                self.texture = Some(ctx.load_texture(
                    "camera_stream",
                    color_image,
                    Default::default()
                ));

                ctx.request_repaint();
            } else {
                println!("failed to load image");
            }
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            if let Some(texture) = &self.texture {
                let img = egui::Image::from_texture(texture)
                    .max_size(ui.available_size())
                    .maintain_aspect_ratio(true);

                ui.add(img);
            } else {
                ui.centered_and_justified(|ui| {
                    ui.label("Camera not connected");
                });
            }
        });
    }
}