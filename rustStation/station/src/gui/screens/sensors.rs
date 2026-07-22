use std::collections::{HashMap, VecDeque};

use egui::Vec2b;
use egui_plot::{Line, Plot, PlotBounds, PlotPoints};
use log::error;

use crate::{gui::{MyApp, ScreensTypes}, sensors::{TelemetryEnum, TelemetryPacket}};

#[derive(PartialEq)]
pub enum MpuSubTab {
    Acceleration,
    Gyroscope,
    Temperature,
}

pub enum SensorTab {
    HCSR04,
    INA226,
    MPU,
    BMP280,
    KY003,
    RFIDRC522,
    RCWL0515,
    KY033,
    ESP,
}

pub struct SensorsScreen {
    pub sensor_selected : Option<SensorTab>,
    pub mpu_sub_tab: MpuSubTab,
}

impl Default for SensorsScreen {
    fn default() -> Self {
        Self {
            sensor_selected : None,
            mpu_sub_tab: MpuSubTab::Acceleration,
        }
    }
}

impl SensorsScreen {
    pub fn show(&mut self, ctx: &egui::Context, data: &VecDeque<(TelemetryPacket, f64)>, screen : &mut ScreensTypes) {

        egui::TopBottomPanel::top("sensors_filter").show(ctx, |ui| {
            ui.horizontal(|ui| {
                if ui.button("Back").clicked() { *screen = ScreensTypes::Main; }
                ui.label("Active sensor :"); 

                // Un bouton d'accueil pour tout désactiver / vue d'ensemble vide au choix
                if ui.selectable_label(self.sensor_selected.is_none(), "None").clicked() {
                    self.sensor_selected = None;
                }

                ui.separator();

                // Piège à clignotement/comparaison : On utilise des variantes factices ou discriminants pour le bouton radio
                if ui.selectable_label(matches!(self.sensor_selected, Some(SensorTab::HCSR04)), "HC-SR04").clicked() {
                    self.sensor_selected = Some(SensorTab::HCSR04);
                }
                if ui.selectable_label(matches!(self.sensor_selected, Some(SensorTab::INA226)), "INA226").clicked() {
                    self.sensor_selected = Some(SensorTab::INA226);
                }
                if ui.selectable_label(matches!(self.sensor_selected, Some(SensorTab::MPU)), "MPU").clicked() {
                    self.sensor_selected = Some(SensorTab::MPU);
                }
                // Si ton BMP utilise la variante MPU dans ton enum actuel (comme mentionné dans tes commentaires)
                if ui.selectable_label(matches!(self.sensor_selected, Some(SensorTab::BMP280)), "BMP280").clicked() {
                    self.sensor_selected = Some(SensorTab::BMP280);
                }
                if ui.selectable_label(matches!(self.sensor_selected, Some(SensorTab::KY003)), "KY-003").clicked() {
                    self.sensor_selected = Some(SensorTab::KY003);
                }
                if ui.selectable_label(matches!(self.sensor_selected, Some(SensorTab::RFIDRC522)), "RFID-RC522").clicked() {
                    self.sensor_selected = Some(SensorTab::RFIDRC522);
                }
                if ui.selectable_label(matches!(self.sensor_selected, Some(SensorTab::RCWL0515)), "RCWL-0515").clicked() {
                    self.sensor_selected = Some(SensorTab::RCWL0515);
                }
                if ui.selectable_label(matches!(self.sensor_selected, Some(SensorTab::KY033)), "KY-033").clicked() {
                    self.sensor_selected = Some(SensorTab::KY033);
                }
                if ui.selectable_label(matches!(self.sensor_selected, Some(SensorTab::ESP)), "ESP").clicked() {
                    self.sensor_selected = Some(SensorTab::ESP);
                }
            });
        });



        let latest_t = data.back().map_or(0.0, |(_, t)| *t);
        const TIME_WINDOW: f64 = 30.0;

        egui::CentralPanel::default().show(ctx, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                let min_t = (latest_t - TIME_WINDOW).max(0.0);

                let mut make_plot = |ui: &mut egui::Ui,
                                    name: &str,
                                    y_label: &str,
                                    y_range: Option<(f64, f64)>,
                                    points_fn: fn(&VecDeque<(TelemetryPacket, f64)>, f64) -> PlotPoints| {
                    Plot::new(name)
                        .height(220.0)
                        .width(ui.available_width())
                        .x_axis_label("Time (s)")
                        .y_axis_label(y_label)
                        .legend(egui_plot::Legend::default().position(egui_plot::Corner::RightTop))
                        
                        .allow_drag(false)
                        .allow_zoom(false)
                        .allow_scroll(false)
                        .allow_boxed_zoom(false)
                        .allow_double_click_reset(false)
                        .show(ui, |plot_ui| {
                            let points = points_fn(data, min_t);
                            plot_ui.line(Line::new(name, points).width(2.5));

                            let (y_min, y_max) = y_range.unwrap_or((0.0, 0.0));
                            // On récupère le timestamp du tout premier point qui existe dans ton historique
                            let first_t = data.front().map(|(_, t)| *t).unwrap_or(min_t);

                            // On interdit à la borne gauche d'aller plus loin que le premier point réel ou sous 0
                            let view_min = f64::max(min_t, first_t).max(0.0);

                            plot_ui.set_plot_bounds(PlotBounds::from_min_max(
                                [view_min, y_min],
                                [latest_t.max(TIME_WINDOW), y_max],
                            ));

                            let auto_y = y_range.is_none();
                            plot_ui.set_auto_bounds(Vec2b::new(false, auto_y));
                        });
                };

                match self.sensor_selected {
                    Some(SensorTab::HCSR04) => {
                        ui.heading("HC-SR04 - Ultrasonic sensor");
                        make_plot( ui, "HC-SR04_DISTANCE [0]", "Distance (cm)", Some((0.0, 600.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::HCSR04(pck) = &p.packet {
                                        if pck.hc_id == 0 {
                                            Some([*t, pck.get_distance_cm()])
                                        } else {
                                            None
                                        }
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });
                        make_plot( ui, "HC-SR04_DISTANCE [1]", "Distance (cm)", Some((0.0, 600.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::HCSR04(pck) = &p.packet {
                                        if pck.hc_id == 1 {
                                            Some([*t, pck.get_distance_cm()])
                                        } else {
                                            None
                                        }
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });
                    },
                    Some(SensorTab::INA226) => {
                        ui.heading("INA226 - Electrical monitoring");

                        make_plot(ui, "INA_BUS_VOLTAGE", "Bus voltage (V)", Some((-6.0, 6.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::INA226(pck) = &p.packet {
                                        Some([*t, pck.get_bus_voltage_v()])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });
                        
                        make_plot(ui, "INA_SHUNT_VOLTAGE", "Shunt voltage (mV)", Some((-2.0, 2.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::INA226(pck) = &p.packet {
                                        Some([*t, pck.get_shunt_voltage_mv()])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });

                        make_plot(ui, "INA_CURRENT", "Current (mA)", Some((0.0, 30.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::INA226(pck) = &p.packet {
                                        Some([*t, pck.get_current_ma()])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });

                        make_plot(ui, "INA_POWER", "Power (mW)", Some((0.0, 50.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::INA226(pck) = &p.packet {
                                        Some([*t, pck.get_power_mw()])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });
                    },
                    Some(SensorTab::KY003) => {
                        ui.heading("KY-003 - Hall sensor");

                        make_plot(ui, "KY_SPEED", "Speed (km/h)", Some((0.0, 100.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::KY003(pck) = &p.packet {
                                        pck.get_speed_km_h().ok().map(|v| [*t, v])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });

                        make_plot(ui, "KY_DISTANCE", "Cumulated distance (m)", None, |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::KY003(pck) = &p.packet {
                                        Some([*t, pck.get_distance_m()])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });
                    },
                    Some(SensorTab::MPU) => {
                        const LOWPASS_ALPHA: f64 = 0.2;
                        const ZUPT_ACCEL_THRESHOLD: f64 = 0.05; // g, post-filtre
                        const ZUPT_GYRO_THRESHOLD: f64 = 3.0;   // deg/s
                        const VEL_DECAY: f64 = 0.98;
                        const BIAS_Y: f64 = 0.0;
                        const BIAS_X: f64 = 0.0;
                        const ZUPT_DEBOUNCE_SAMPLES: u32 = 3;

                        ui.heading("MPU - Inertial measurement unit (IMU)");

                        ui.horizontal(|ui| {
                            ui.label("Data :");
                            if ui.selectable_label(self.mpu_sub_tab == MpuSubTab::Acceleration, "Accelerometer").clicked() {
                                self.mpu_sub_tab = MpuSubTab::Acceleration;
                            }
                            if ui.selectable_label(self.mpu_sub_tab == MpuSubTab::Gyroscope, "Gyroscope").clicked() {
                                self.mpu_sub_tab = MpuSubTab::Gyroscope;
                            }
                            if ui.selectable_label(self.mpu_sub_tab == MpuSubTab::Temperature, "Temperature").clicked() {
                                self.mpu_sub_tab = MpuSubTab::Temperature;
                            }
                        });
                        ui.separator();

                        match self.mpu_sub_tab {
                            MpuSubTab::Acceleration => {
                                make_plot(ui, "MPU_VEL_X", "Vitesse X (m/s)", Some((-3.0, 3.0)), |data, min_t| {
                                    let mut accel_filt: Option<f64> = None;
                                    let mut vel: f64 = 0.0;
                                    let mut last_t: Option<f64> = None;
                                    let mut still_count_x = 0;

                                    data.iter()
                                        .skip_while(|(_, t)| *t < min_t)
                                        .filter_map(|(p, t)| {
                                            if let TelemetryEnum::MPU(pck) = &p.packet {
                                                let (ax_raw, _, _) = pck.get_accel_g();
                                                let ax_raw = ax_raw - BIAS_X;
                                                let gyro_z = pck.get_gyro_deg_s().2;

                                                let prev_filt = accel_filt.unwrap_or(ax_raw);
                                                let ax_filt = LOWPASS_ALPHA * ax_raw + (1.0 - LOWPASS_ALPHA) * prev_filt;
                                                accel_filt = Some(ax_filt);

                                                if let Some(lt) = last_t {
                                                    let dt = (*t - lt).max(0.0);
                                                    vel += ax_filt * 9.81 * dt;

                                                    if ax_filt.abs() < ZUPT_ACCEL_THRESHOLD && gyro_z.abs() < ZUPT_GYRO_THRESHOLD {
                                                        still_count_x += 1;
                                                    } else {
                                                        still_count_x = 0;
                                                    }
                                                    if still_count_x >= ZUPT_DEBOUNCE_SAMPLES {
                                                        vel = 0.0;
                                                    } else {
                                                        vel *= VEL_DECAY;
                                                    }
                                                }
                                                last_t = Some(*t);

                                                Some([*t, vel])
                                            } else { None }
                                        })
                                        .collect()
                                });

                                make_plot(ui, "MPU_VEL_Y", "Vitesse Y (m/s)", Some((-3.0, 3.0)), |data, min_t| {
                                    let mut accel_filt: Option<f64> = None;
                                    let mut vel: f64 = 0.0;
                                    let mut last_t: Option<f64> = None;
                                    let mut still_count_y = 0;

                                    data.iter()
                                        .skip_while(|(_, t)| *t < min_t)
                                        .filter_map(|(p, t)| {
                                            if let TelemetryEnum::MPU(pck) = &p.packet {
                                                let (_, ay_raw, _) = pck.get_accel_g();
                                                let ay_raw = ay_raw - BIAS_Y;
                                                let gyro_z = pck.get_gyro_deg_s().2;

                                                let prev_filt = accel_filt.unwrap_or(ay_raw);
                                                let ay_filt = LOWPASS_ALPHA * ay_raw + (1.0 - LOWPASS_ALPHA) * prev_filt;
                                                accel_filt = Some(ay_filt);

                                                if let Some(lt) = last_t {
                                                    let dt = (*t - lt).max(0.0);
                                                    vel += ay_filt * 9.81 * dt;

                                                    if ay_filt.abs() < ZUPT_ACCEL_THRESHOLD && gyro_z.abs() < ZUPT_GYRO_THRESHOLD {
                                                        still_count_y += 1;
                                                    } else {
                                                        still_count_y = 0;
                                                    }
                                                    if still_count_y >= ZUPT_DEBOUNCE_SAMPLES {
                                                        vel = 0.0;
                                                    } else {
                                                        vel *= VEL_DECAY;
                                                    }
                                                }
                                                last_t = Some(*t);

                                                Some([*t, vel])
                                            } else { None }
                                        })
                                        .collect()
                                });

                                make_plot(ui, "MPU_ACCEL_X", "Acceleration X (g)", Some((-1.0, 1.0)), |data, min_t| {
                                    let mut filtered_accum: Option<f64> = None;
                                    
                                    data.iter()
                                        .skip_while(|(_, t)| *t < min_t)
                                        .filter_map(|(p, t)| {
                                            if let TelemetryEnum::MPU(pck) = &p.packet {
                                                let (ax_raw, _, _) = pck.get_accel_g();
                                                let prev = filtered_accum.unwrap_or(ax_raw); // premier point = valeur brute, pas de saut au démarrage
                                                let new_val = LOWPASS_ALPHA * ax_raw + (1.0 - LOWPASS_ALPHA) * prev;
                                                filtered_accum = Some(new_val);
                                                Some([*t, new_val])
                                            } else { None }
                                        })
                                        .collect()
                                });

                                make_plot(ui, "MPU_ACCEL_Y", "Acceleration Y (g)", Some((-1.0, 1.0)), |data, min_t| {
                                    let mut filtered_accum: Option<f64> = None;
                                    
                                    data.iter()
                                        .skip_while(|(_, t)| *t < min_t)
                                        .filter_map(|(p, t)| {
                                            if let TelemetryEnum::MPU(pck) = &p.packet {
                                                let (_, ay_raw, _) = pck.get_accel_g();
                                                let prev = filtered_accum.unwrap_or(ay_raw); // premier point = valeur brute, pas de saut au démarrage
                                                let new_val = LOWPASS_ALPHA * ay_raw + (1.0 - LOWPASS_ALPHA) * prev;
                                                filtered_accum = Some(new_val);
                                                Some([*t, new_val])
                                            } else { None }
                                        })
                                        .collect()
                                });

                                make_plot(ui, "MPU_ACCEL_Z", "Acceleration Z (g)", Some((-3.0, 3.0)), |data, min_t| {
                                    data.iter()
                                        .skip_while(|(_, t)| *t < min_t)
                                        .filter_map(|(p, t)| {
                                            if let TelemetryEnum::MPU(pck) = &p.packet {
                                                let (_, _, az) = pck.get_accel_g();
                                                Some([*t, az])
                                            } else { None }
                                        })
                                        .collect()
                                });
                            }

                            MpuSubTab::Gyroscope => {
                                make_plot(ui, "MPU_GYRO_X", "Gyro X (°/s)", Some((-300.0, 300.0)), |data, min_t| {
                                    data.iter()
                                        .skip_while(|(_, t)| *t < min_t)
                                        .filter_map(|(p, t)| {
                                            if let TelemetryEnum::MPU(pck) = &p.packet {
                                                let (gx, _, _) = pck.get_gyro_deg_s();
                                                Some([*t, gx])
                                            } else { None }
                                        })
                                        .collect()
                                });

                                make_plot(ui, "MPU_GYRO_Y", "Gyro Y (°/s)", Some((-300.0, 300.0)), |data, min_t| {
                                    data.iter()
                                        .skip_while(|(_, t)| *t < min_t)
                                        .filter_map(|(p, t)| {
                                            if let TelemetryEnum::MPU(pck) = &p.packet {
                                                let (_, gy, _) = pck.get_gyro_deg_s();
                                                Some([*t, gy])
                                            } else { None }
                                        })
                                        .collect()
                                });

                                make_plot(ui, "MPU_GYRO_Z", "Gyro Z (°/s)", Some((-300.0, 300.0)), |data, min_t| {
                                    data.iter()
                                        .skip_while(|(_, t)| *t < min_t)
                                        .filter_map(|(p, t)| {
                                            if let TelemetryEnum::MPU(pck) = &p.packet {
                                                let (_, _, gz) = pck.get_gyro_deg_s();
                                                Some([*t, gz])
                                            } else { None }
                                        })
                                        .collect()
                                });
                            }

                            MpuSubTab::Temperature => {
                                make_plot(ui, "MPU_TEMP", "MPU Temperature (°C)", Some((0.0, 50.0)), |data, min_t| {
                                    data.iter()
                                        .skip_while(|(_, t)| *t < min_t)
                                        .filter_map(|(p, t)| {
                                            if let TelemetryEnum::MPU(pck) = &p.packet {
                                                Some([*t, pck.get_temperature_chip_deg()])
                                            } else { None }
                                        })
                                        .collect()
                                });
                            }
                        }
                    },

                    Some(SensorTab::BMP280) => {
                        ui.heading("BMP280 - Pressure & temperature");

                        make_plot(ui, "BMP_TEMP", "BMP Temperature (°C)", Some((0.0, 50.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    // Note : Ton ancien code cherchait la température du BMP dans p.imu, 
                                    // donc j'ai mappé sur TelemetryEnum::MPU comme pour le MPU au-dessus.
                                    if let TelemetryEnum::MPU(pck) = &p.packet {
                                        Some([*t, pck.get_temperature_deg()])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });

                        make_plot(ui, "BMP_PRESSURE", "Pressure (bar)", Some((0.8, 1.2)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::MPU(pck) = &p.packet {
                                        Some([*t, pck.get_pressure_bar()])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });
                    },
                    Some(SensorTab::RFIDRC522) => {
                        ui.group(|ui| {
                            ui.vertical(|ui| {
                                ui.heading("RFID-RC522 - tag UID scan");
                                ui.separator();

                                let mut uid_counts: HashMap<String, usize> = HashMap::new();
                                let mut last_scanned: Option<(String, f64)> = None;

                                for (p, t) in data.iter() {
                                    if let TelemetryEnum::RFIDRC522(pck) = &p.packet {
                                        let uid = pck.get_uid_string();
                                        *uid_counts.entry(uid.clone()).or_insert(0) += 1;
                                        last_scanned = Some((uid, *t));
                                    }
                                }

                                if let Some((uid, t)) = last_scanned {
                                    ui.horizontal(|ui| {
                                        ui.strong("Last tag detected :");
                                        ui.colored_label(egui::Color32::LIGHT_GREEN, format!(" {} ", uid));
                                        ui.label(format!("at t = {:.2}s", t));
                                    });
                                } else {
                                    ui.weak("No tag detected");
                                }

                                ui.add_space(8.0);

                                egui::Grid::new("rfid_table")
                                    .striped(true)
                                    .spacing([40.0, 4.0])
                                    .show(ui, |ui| {
                                        ui.label(egui::RichText::new("Unique Identifier (UID").strong());
                                        ui.label(egui::RichText::new("Scan count").strong());
                                        ui.end_row();

                                        let mut sorted_counts: Vec<(&String, &usize)> = uid_counts.iter().collect();
                                        sorted_counts.sort_by(|a, b| b.1.cmp(a.1).then_with(|| a.0.cmp(b.0))); 

                                        for (uid, count) in sorted_counts {
                                            ui.label(uid);
                                            ui.label(count.to_string());
                                            ui.end_row();
                                        }
                                    });
                            });
                        });
                    },
                    Some(SensorTab::RCWL0515) => {
                        ui.heading("RCWL-0515 : Doppler radar motion detector");
                        ui.separator();

                        let mut currently_detected = false;
                        let mut history: Vec<(f64, bool)> = Vec::new();

                        // 💡 1. On extrait l'état actuel ET on construit l'historique des événements
                        for (p, t) in data.iter() {
                            if let TelemetryEnum::RCWL0515(motion) = &p.packet {
                                currently_detected = motion.detection;
                                
                                // On enregistre chaque changement d'état dans notre historique
                                history.push((*t, motion.detection));
                            }
                        }

                        // 💡 2. Affichage du gros widget d'état actuel (Rouge/Vert)
                        ui.group(|ui| {
                            ui.vertical_centered_justified(|ui| {
                                if currently_detected {
                                    let text = egui::RichText::new(" 🚨 MOVEMENT DETECTED ").size(24.0).strong().color(egui::Color32::WHITE);
                                    ui.colored_label(egui::Color32::from_rgb(200, 30, 30), text);
                                } else {
                                    let text = egui::RichText::new("  RAS - No movement ").size(24.0).strong().color(egui::Color32::WHITE);
                                    ui.colored_label(egui::Color32::from_rgb(30, 150, 30), text);
                                }
                            });
                        });

                        ui.add_space(15.0);

                        // 💡 3. SECTION HISTORIQUE
                        ui.collapsing("📋 Record of events", |ui| {
                            if history.is_empty() {
                                ui.weak("No event stored");
                            } else {
                                // On inverse l'historique pour afficher le plus RÉCENT en premier
                                history.reverse();

                                egui::Grid::new("rcwl_history_table")
                                    .striped(true)
                                    .spacing([40.0, 6.0])
                                    .show(ui, |ui| {
                                        ui.label(egui::RichText::new("Time (s)").strong());
                                        ui.label(egui::RichText::new("Event").strong());
                                        ui.end_row();

                                        // On affiche seulement les 10 derniers événements pour pas flooder l'écran
                                        for (t, motion) in history.iter().take(10) {
                                            ui.label(format!("{:.2}s", t));

                                            if *motion {
                                                ui.colored_label(egui::Color32::LIGHT_RED, "🔴 Movement start");
                                            } else {
                                                ui.colored_label(egui::Color32::LIGHT_GREEN, "🟢 Movement ending");
                                            }
                                            ui.end_row();
                                        }
                                    });
                            }
                        });
                    },
                    Some(SensorTab::KY033) => {
                        ui.heading("KY-033 - Color tracking sensor");
                        make_plot( ui, "KY033 Pulses", "Number of pulses", Some((0.0, 100.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::KY033(pck) = &p.packet {
                                        Some([*t, pck.pulses as f64])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });
                    },
                    Some(SensorTab::ESP) => {
                        ui.heading("ESP32-S3 - System & Hardware Monitoring");

                        // --- 1. AFFICHAGE DES VALEURS LUES EN DIRECT (Dernier paquet reçu) ---
                        // On cherche le dernier point dans l'historique qui contient un paquet ESP
                        if let Some(last_pck) = data.iter().rev().find_map(|(p, _)| {
                            if let TelemetryEnum::ESP(pck) = &p.packet { Some(pck) } else { None }
                        }) {
                            ui.group(|ui| {
                                ui.horizontal(|ui| {
                                    ui.label(format!("🔄 Dernier Reset : {}", last_pck.reset_reason.as_str()));
                                    ui.separator();
                                    ui.label(format!("📦 Paquets reçus : {}", last_pck.nb_packets));
                                    ui.separator();
                                    ui.label(format!("📐 Angle : {}°", last_pck.angle));
                                    ui.separator();
                                    ui.label(format!("⚙️ Moteur : {}", last_pck.motor));
                                });
                                
                                ui.add_space(5.0);
                                
                                // Barres de progression pour la RAM (Basé sur le max dispo de ton N16R8)
                                let dram_total = 320 * 1024;
                                let dram_used = dram_total - last_pck.free_dram;
                                ui.horizontal(|ui| {
                                    ui.label("DRAM Interne :");
                                    ui.add(egui::ProgressBar::new(dram_used as f32 / dram_total as f32)
                                        .text(format!("{:.1} KB libres", last_pck.free_dram as f32 / 1024.0)));
                                });

                                let psram_total = 8 * 1024 * 1024;
                                let psram_used = psram_total - last_pck.free_psram;
                                ui.horizontal(|ui| {
                                    ui.label("PSRAM Externe :");
                                    ui.add(egui::ProgressBar::new(psram_used as f32 / psram_total as f32)
                                        .text(format!("{:.2} MB libres", last_pck.free_psram as f32 / (1024.0 * 1024.0))));
                                });
                            });
                        }

                        ui.add_space(10.0);

                        // --- 2. GRAPHIQUES TEMPORELS (Historique) ---
                        
                        // Température de la puce
                        make_plot(ui, "ESP_TEMPERATURE", "Température (°C)", Some((0.0, 80.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::ESP(pck) = &p.packet {
                                        Some([*t, pck.esp_deg as f64])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });

                        // Puissance du signal Wi-Fi
                        make_plot(ui, "ESP_WIFI_RSSI", "Signal RSSI (dBm)", Some((-100.0, 0.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::ESP(pck) = &p.packet {
                                        Some([*t, pck.rssi as f64])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });

                        // Charge CPU du Core 0 (Réseau / Wi-Fi)
                        make_plot(ui, "ESP_CPU_CORE_0", "CPU Core 0 (%)", Some((0.0, 100.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::ESP(pck) = &p.packet {
                                        Some([*t, pck.core0 as f64])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });

                        // Charge CPU du Core 1 (Ton application / Moteurs / Capteurs)
                        make_plot(ui, "ESP_CPU_CORE_1", "CPU Core 1 (%)", Some((0.0, 100.0)), |data, min_t| {
                            data.iter()
                                .skip_while(|(_, t)| *t < min_t)
                                .filter_map(|(p, t)| {
                                    if let TelemetryEnum::ESP(pck) = &p.packet {
                                        Some([*t, pck.core1 as f64])
                                    } else {
                                        None
                                    }
                                })
                                .collect()
                        });
                    },
                    None => {
                        ui.centered_and_justified(|ui| {
                            ui.weak("Select a sensor...");
                        });
                    },
                }
            });
        });
    }
}