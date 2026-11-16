use std::{net::UdpSocket, sync::{Arc, Mutex, mpsc::{Receiver, Sender}}};

use log::error;

use crate::{config::AppConfig, gui::ScreensTypes, ota::{OtaServerStatus, fetch_latest_bin, serve_firmware}, udp::ConfigCommandType};


#[derive(PartialEq, Clone)]
pub enum EspChoice {
    ESP0(String),
    ESP1(String),
}

impl EspChoice {
    pub fn esp_str(&self) -> String {
        match self {
            EspChoice::ESP0(_) => "ESP0".to_string(),
            EspChoice::ESP1(_) => "ESP1".to_string(),
        }
    }
}

pub struct OtaScreen {
    pub firmware_path: Option<std::path::PathBuf>,
    pub server_status: Arc<Mutex<OtaServerStatus>>,
    pub server_started: bool,
    pub port: u16,
    pub socket_udp_config: UdpSocket,
    pub esp_selected: Option<EspChoice>,
    tx_fetch_result: Sender<Result<(), String>>,
    rx_fetch_result: Receiver<Result<(), String>>,
    fetch_error: Option<String>
}

impl Default for OtaScreen {
    fn default() -> Self {
        let (tx_fetch_result, rx_fetch_result) = std::sync::mpsc::channel();
        Self {
            firmware_path: None,
            server_status: Arc::new(Mutex::new(OtaServerStatus::default())),
            server_started: false,
            port: 8070,
            socket_udp_config: UdpSocket::bind("0.0.0.0:0").unwrap(),
            esp_selected: None,
            tx_fetch_result,
            rx_fetch_result,
            fetch_error: None,
        }
    }
}

impl OtaScreen {
    pub fn show(&mut self, ctx: &egui::Context, screen: &mut ScreensTypes, config_ota: &AppConfig) {
        while let Ok(result) = self.rx_fetch_result.try_recv() {
            match result {
                Ok(()) => { self.firmware_path = Some("./firmware.bin".into()); self.fetch_error = None; }
                Err(e) => { self.fetch_error = Some(e); }
            }
        }

        egui::CentralPanel::default().show(ctx, |ui| {

            egui::ComboBox::from_label("Select ESP for OTA")
                .selected_text(match &self.esp_selected {
                    Some(esp) => format!("ESP32 ({})", esp.esp_str()),
                    None => "Choose an ESP...".to_string(),
                })
                .show_ui(ui, |ui| {
                    let (esp0, esp1) = 
                        (EspChoice::ESP0(config_ota.ip_addr_esp0.clone()), EspChoice::ESP1(config_ota.ip_addr_esp1.clone()));
                    ui.selectable_value(&mut self.esp_selected,
                        Some(esp0.clone()), format!("{}", esp0.esp_str()));
                    ui.selectable_value(&mut self.esp_selected,
                        Some(esp1.clone()), format!("{}", esp1.esp_str()));
                }
            );

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
                    let server_bin = config_ota.server_bin.clone();
                    let tx = self.tx_fetch_result.clone();
                    std::thread::spawn(move || {
                        let result = fetch_latest_bin(&server_bin);
                        let _ = tx.send(result);
                    });
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

                let can_trigger = self.server_started && self.esp_selected.is_some();
                if let (true, Some(esp)) = 
                (ui.add_enabled(can_trigger, egui::Button::new("Launch OTA update on ESP")).clicked(),
                self.esp_selected.clone()) {
                    let cmd = vec![ConfigCommandType::Ota.to_u8()];
                    let ip_addr = match esp { EspChoice::ESP0(val) => val, EspChoice::ESP1(val) => val};
                    let addr = format!("{}:{}", ip_addr, config_ota.udp_port_config);
                    if let Err(e) = self.socket_udp_config.send_to(&cmd, &addr) {
                        error!("Error sending config to {addr}: {e}");
                    };
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