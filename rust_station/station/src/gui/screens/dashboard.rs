use std::collections::{HashMap, VecDeque};

use crate::{gui::ScreensTypes, sensors::{SensorKey, TelemetryEnum, TelemetryPacket}};

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ColorMode {
    Rgb,
    White,
}

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum LightEffect {
    None,
    Rainbow,
    Flash,
    Breathing,
    Read,
    Night,
}

impl LightEffect {
    pub const ALL: [LightEffect; 6] = [
        LightEffect::None,
        LightEffect::Rainbow,
        LightEffect::Flash,
        LightEffect::Breathing,
        LightEffect::Read,
        LightEffect::Night,
    ];

    pub fn label(&self) -> &'static str {
        match self {
            Self::None => "None",
            Self::Rainbow => "Rainbow",
            Self::Flash => "Flash",
            Self::Breathing => "Breathing",
            Self::Read => "Read",
            Self::Night => "Night",
        }
    }
}

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum PowerOnBehavior {
    Off,
    On,
    Previous,
}

impl PowerOnBehavior {

    pub const ALL: [PowerOnBehavior; 3] = [
        PowerOnBehavior::Off,
        PowerOnBehavior::On,
        PowerOnBehavior::Previous,
    ];

    pub fn label(&self) -> &'static str {
        match self {
            Self::Off => "OFF",
            Self::On => "ON",
            Self::Previous => "Previous State",
        }
    }
}

#[derive(Debug)]
pub struct DashboardScreen {
    pub led_power: bool,
    pub led_mode: ColorMode,
    pub led_color: [f32; 3],
    pub led_color_temp_k: u16, // 2700 K à 6500 K
    pub led_brightness: u8,    // 0 à 100 %
    pub led_effect: LightEffect,
    pub led_power_on: PowerOnBehavior,
    pub led_transition: f32,   // 0.0s à 5.0s
    pub selected_esp_id: Option<u8>,
}

impl Default for DashboardScreen {
    fn default() -> Self {
        Self {
            led_power: false,
            led_mode: ColorMode::White,
            led_color: [1.0, 1.0, 1.0],
            led_color_temp_k: 4000,
            led_brightness: 100,
            led_effect: LightEffect::None,
            led_power_on: PowerOnBehavior::Previous,
            led_transition: 0.4,
            selected_esp_id: None,
        }
    }
}

impl DashboardScreen {
    pub fn show(&mut self, ctx: &egui::Context, 
        devices: &HashMap<u8, [VecDeque<(TelemetryPacket, f64)>; SensorKey::COUNT]>,
        screen : &mut ScreensTypes
    ) {
        
        egui::SidePanel::left("esp_sidebar").min_width(150.0).show(ctx, |ui| {
            ui.heading("Connected ESPs");
            ui.separator();

            let mut ids: Vec<u8> = devices.keys().cloned().collect();
            ids.sort(); // Pour garder un ordre stable dans la liste

            for id in ids {
                let is_selected = self.selected_esp_id == Some(id);
                if ui.selectable_label(is_selected, format!("ESP #{0}", id)).clicked() {
                    self.selected_esp_id = Some(id);
                }
            }

            if devices.is_empty() {
                ui.add_space(10.0);
                ui.weak("Waiting for ESPs...");
            }
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            if ui.button("Back").clicked() { *screen = ScreensTypes::Main; }
            if let Some(active_id) = self.selected_esp_id {
                if let Some(data) = devices.get(&active_id) {
                    egui::ScrollArea::vertical().show(ui, |ui| {
                        let latest_dht11 = data[SensorKey::DHT11.index()].back();
                        let latest_photo = data[SensorKey::Photosensor.index()].back();
                        let latest_ina226 = data[SensorKey::INA226.index()].back();
                        let latest_rcwl = data[SensorKey::Rcwl0515.index()].back();

                        ui.heading("Monitoring Dashboard");
                        ui.add_space(10.0);

                        let col_width = ((ui.available_width() - 15.0) / 2.0).max(180.0);

                        // Cards dashboard
                        egui::Grid::new("dashboard_grid")
                            .spacing([15.0, 15.0])
                            .min_col_width(col_width)
                            .show(ui, |ui| {
                                
                                // --- MOVEMENT DETECTION ---
                                card_frame(ui, |ui| {
                                    ui.label(egui::RichText::new("Doppler Radar").small().weak());
                                    ui.add_space(5.0);

                                    if let Some((TelemetryPacket { 
                                        packet: TelemetryEnum::RCWL0515(rcwl), .. }, _)) = latest_rcwl {
                                        if rcwl.detection {
                                            ui.colored_label(
                                                egui::Color32::from_rgb(230, 50, 50),
                                                egui::RichText::new("MOVEMENT DETECTED").size(18.0).strong(),
                                            );
                                        } else {
                                            ui.colored_label(
                                                egui::Color32::from_rgb(40, 180, 80),
                                                egui::RichText::new("Clear (NO MOTION)").size(18.0).strong(),
                                            );
                                        }
                                    } else {
                                        ui.weak("No signal");
                                    }
                                });

                                // --- ENVIRONMENT ---
                                card_frame(ui, |ui| {
                                    ui.label(egui::RichText::new("Environment (DHT11)").small().weak());
                                    ui.add_space(5.0);

                                    if let Some((TelemetryPacket { 
                                        packet: TelemetryEnum::DHT11(dht), .. }, _)) = latest_dht11 {
                                        ui.horizontal(|ui| {
                                            ui.vertical(|ui| {
                                                ui.label("Temperature");
                                                ui.label(
                                                    egui::RichText::new(format!("{:.1} °C", dht.temperature))
                                                        .size(22.0)
                                                        .strong()
                                                        .color(egui::Color32::LIGHT_BLUE),
                                                );
                                            });
                                            ui.add_space(15.0);
                                            ui.vertical(|ui| {
                                                ui.label("Humidity");
                                                ui.label(
                                                    egui::RichText::new(format!("{:.1} %", dht.humidity))
                                                        .size(22.0)
                                                        .strong()
                                                        .color(egui::Color32::KHAKI),
                                                );
                                            });
                                        });
                                    } else {
                                        ui.weak("No data");
                                    }
                                });

                                ui.end_row();

                                // --- POWER MONITOR ---
                                // power is only for DHT-11 & KY018 sensors
                                card_frame(ui, |ui| {
                                    ui.label(egui::RichText::new("Power Monitor (INA226)").small().weak());
                                    ui.add_space(5.0);

                                    if let Some((TelemetryPacket { 
                                        packet: TelemetryEnum::INA226(ina), .. }, _)) = latest_ina226 {
                                        ui.horizontal(|ui| {
                                            metric_value(ui, "Voltage", &format!("{:.2} V", ina.get_bus_voltage_v()));
                                            ui.add_space(10.0);
                                            metric_value(ui, "Current", &format!("{:.1} mA", ina.get_current_ma()));
                                            ui.add_space(10.0);
                                            metric_value(ui, "Power", &format!("{:.1} mW", ina.get_power_mw()));
                                        });
                                    } else {
                                        ui.weak("No data");
                                    }
                                });

                                // --- LIGHT ---
                                // TODO: max value (how much? 150 or 200?) - current value = percentage of light 
                                card_frame(ui, |ui| {
                                    ui.label(egui::RichText::new("Ambient Light (KY018)").small().weak());
                                    ui.add_space(5.0);

                                    if let Some((TelemetryPacket { 
                                        packet: TelemetryEnum::PHOTOSENSOR(photo), .. }, _)) = latest_photo {
                                        let val = photo.raw_value as f64;
                                        ui.label(egui::RichText::new(format!("{:.0}", val)).size(22.0).strong());
                                        
                                        let progress = (val / 200.0).clamp(0.0, 1.0) as f32;
                                        ui.add(egui::ProgressBar::new(progress).show_percentage());
                                    } else {
                                        ui.weak("No data");
                                    }
                                });

                                ui.end_row();

                                // --- SMART LIGHT CONTROL (Tuya Zigbee E14) ---
                                self.render_tuya_card(ui);

                                ui.end_row();

                            });

                        });
                    } else {
                        ui.weak(format!("No valid sensor data for ID #{}", active_id));
                    }
                } else {
                    ui.weak(format!("No valid ESP ID"));
                }
            });
    }

    fn render_tuya_card(&mut self, ui: &mut egui::Ui) {
        card_frame(ui, |ui| {
            ui.label(egui::RichText::new("💡 Tuya E14 RGBCW Smart Light").small().weak());
            ui.add_space(5.0);

            let mut changed = false;

            // 1. Bouton principal ON / OFF
            ui.horizontal(|ui| {
                let toggle_text = if self.led_power { "ON" } else { "OFF" };
                if ui.selectable_label(self.led_power, toggle_text).clicked() {
                    self.led_power = !self.led_power;
                    changed = true;
                }
            });

            ui.add_space(5.0);

            // Contrôles actifs seulement si la lampe est sous tension
            ui.add_enabled_ui(self.led_power, |ui| {
                // 2. Commutation Mode Couleur vs Mode Blanc
                ui.horizontal(|ui| {
                    ui.label("Mode:");
                    if ui.selectable_value(&mut self.led_mode, ColorMode::White, "⚪ White").changed() {
                        changed = true;
                    }
                    if ui.selectable_value(&mut self.led_mode, ColorMode::Rgb, "🎨 RGB").changed() {
                        changed = true;
                    }
                });

                ui.add_space(5.0);

                // 3. Sélecteur dynamique selon le mode actif
                match self.led_mode {
                    ColorMode::Rgb => {
                        ui.horizontal(|ui| {
                            ui.label("Color:");
                            if ui.color_edit_button_rgb(&mut self.led_color).changed() {
                                changed = true;
                            }
                        });
                    }
                    ColorMode::White => {
                        ui.horizontal(|ui| {
                            ui.label("Temp:");
                            if ui.add(egui::Slider::new(&mut self.led_color_temp_k, 2700..=6500).suffix(" K")).changed() {
                                changed = true;
                            }
                        });
                    }
                }

                // 4. Slider de Luminosité
                ui.horizontal(|ui| {
                    ui.label("Brightness:");
                    if ui.add(egui::Slider::new(&mut self.led_brightness, 0..=100).suffix("%")).changed() {
                        changed = true;
                    }
                });

                // 5. Effets lumineux préprogrammés
                ui.horizontal(|ui| {
                    ui.label("Effect:");
                    egui::ComboBox::from_id_salt("tuya_effect_combo")
                        .selected_text(self.led_effect.label())
                        .show_ui(ui, |ui| {
                            for effect in LightEffect::ALL {
                                if ui.selectable_value(&mut self.led_effect, effect, effect.label()).changed() {
                                    changed = true;
                                }
                            }
                        });
                });

                ui.add_space(5.0);

                // 6. Section déroulante pour les paramètres avancés Zigbee
                egui::CollapsingHeader::new("⚙️ Advanced Settings").show(ui, |ui| {
                    // Temps de transition (0 à 5 secondes)
                    ui.horizontal(|ui| {
                        ui.label("Transition:");
                        if ui.add(egui::Slider::new(&mut self.led_transition, 0.0..=5.0).suffix(" s")).changed() {
                            changed = true;
                        }
                    });

                    // Comportement à la mise sous tension (Power On)
                    ui.horizontal(|ui| {
                        ui.label("Power-On State:");
                        egui::ComboBox::from_id_salt("tuya_poweron_combo")
                            .selected_text(self.led_power_on.label())
                            .show_ui(ui, |ui| {
                                for behavior in PowerOnBehavior::ALL {
                                    if ui.selectable_value(&mut self.led_power_on, behavior, behavior.label()).changed() {
                                        changed = true;
                                    }
                                }
                            });
                    });
                });
            });

            // Émission de la commande Zigbee/MQTT si une valeur a été modifiée
            if changed {
                self.send_zigbee_payload();
            }
        });
    }

    fn send_zigbee_payload(&self) {
        // Envoi au format standard Zigbee2MQTT / Tuya
        let r = (self.led_color[0] * 255.0) as u8;
        let g = (self.led_color[1] * 255.0) as u8;
        let b = (self.led_color[2] * 255.0) as u8;

        println!(
            "Zigbee CMD -> state: {}, mode: {:?}, RGB: ({},{},{}), Temp: {}K, Brightness: {}%, Effect: {:?}, Transition: {}s, PowerOn: {:?}",
            if self.led_power { "ON" } else { "OFF" },
            self.led_mode, r, g, b, self.led_color_temp_k,
            self.led_brightness, self.led_effect, self.led_transition, self.led_power_on
        );
    }
}

// Card frame helper
fn card_frame<R>(ui: &mut egui::Ui, add_contents: impl FnOnce(&mut egui::Ui) -> R) -> R {
    egui::Frame::group(ui.style())
        .corner_radius(8.0)
        .inner_margin(12.0)
        .fill(ui.visuals().widgets.noninteractive.bg_fill)
        .show(ui, add_contents)
        .inner
}

// Metric text helper
fn metric_value(ui: &mut egui::Ui, label: &str, value: &str) {
    ui.vertical(|ui| {
        ui.label(egui::RichText::new(label).small());
        ui.label(egui::RichText::new(value).size(18.0).strong());
    });
}