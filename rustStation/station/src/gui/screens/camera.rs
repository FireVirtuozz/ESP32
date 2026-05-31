use std::collections::VecDeque;
use egui::{Color32, FontId, RichText, ScrollArea, TextEdit, Ui, epaint::image};
use ::image::load_from_memory;
use log::{debug, error, warn};

use crate::config::{AppConfig, CamFormat};


pub struct CameraScreen {
    texture: Option<egui::TextureHandle>,
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
        }
    }
}


impl CameraScreen {
    pub fn show(&mut self, ctx: &egui::Context, frame: &mut Option<Vec<u8>>, config_egui: &AppConfig) {
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

                        ctx.request_repaint();
                    } else {
                        error!("failed to load image");
                    }
                }, 
                CamFormat::YUV => {
                    let (cam_height, cam_width) = config_egui.cam_res.dimensions();
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