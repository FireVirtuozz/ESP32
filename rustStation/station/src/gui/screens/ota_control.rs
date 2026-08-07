use std::{net::UdpSocket, sync::{Arc, Mutex}};

use crate::{config::AppConfig, gui::ScreensTypes, ota::{OtaServerStatus, fetch_latest_bin, serve_firmware}};

pub struct OtaScreen {
    pub firmware_path: Option<std::path::PathBuf>,
    pub server_status: Arc<Mutex<OtaServerStatus>>,
    pub server_started: bool,
    pub port: u16,
    pub socket_udp_config: UdpSocket,
}

impl Default for OtaScreen {
    fn default() -> Self {
        Self {
            firmware_path: None,
            server_status: Arc::new(Mutex::new(OtaServerStatus::default())),
            server_started: false,
            port: 8070,
            socket_udp_config: UdpSocket::bind("0.0.0.0:0").unwrap(),
        }
    }
}

impl OtaScreen {
    pub fn show(&mut self, ctx: &egui::Context, screen: &mut ScreensTypes, config_ota: &AppConfig) {
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("OTA Firmware Update");
            ui.separator();

            ui.horizontal(|ui| {
                ui.label("Firmware :");
                if ui.button("Choose .bin").clicked() {
                    if let Some(path) = rfd::FileDialog::new().add_filter("bin", &["bin"]).pick_file() {
                        self.firmware_path = Some(path);
                    }
                }
                if ui.button("Fetch latest on server").clicked() {
                    if fetch_latest_bin(&config_ota.server_bin).is_ok() {
                        self.firmware_path = Some("./firmware.bin".into());
                    }
                }
                if let Some(p) = &self.firmware_path {
                    ui.label(p.file_name().unwrap().to_string_lossy());
                }
            });

            ui.add(egui::DragValue::new(&mut self.port).prefix("Port: "));

            ui.horizontal(|ui| {
                let can_start = self.firmware_path.is_some() && !self.server_started;
                if ui.add_enabled(can_start, egui::Button::new("Start OTA server")).clicked() {
                    let path = self.firmware_path.as_ref().unwrap().to_string_lossy().to_string();
                    serve_firmware(&format!("0.0.0.0:{}", self.port), path, self.server_status.clone());
                    self.server_started = true;
                }

                let can_trigger = self.server_started;
                if ui.add_enabled(can_trigger, egui::Button::new("Launch OTA update on ESP")).clicked() {
                    let cmd = vec![3];
                    self.socket_udp_config.send_to(&cmd, "192.168.1.58:3334")
                        .expect("couldn't bind to address");
                }
            });

            ui.separator();
            let status = self.server_status.lock().unwrap();
            ui.label(format!("HTTP server: {}", if status.running { "Active" } else { "Stopped" }));
            if let Some(ip) = &status.last_client_ip {
                ui.label(format!("Last client: {}", ip));
            }
            ui.label(format!("Bytes served: {} KB", status.bytes_served / 1024));
            drop(status);

            ui.with_layout(egui::Layout::bottom_up(egui::Align::LEFT), |ui| {
                if ui.button("Back").clicked() {
                    *screen = ScreensTypes::Main;
                }
            });
        });
    }
}