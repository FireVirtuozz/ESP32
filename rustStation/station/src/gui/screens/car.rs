use std::{collections::VecDeque, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc}, time::Instant};

use egui_plot::{Line, Plot, PlotPoints, Points};

use crate::{controller::ControllerPacket, gui::ScreensTypes, sensors::{TelemetryEnum, TelemetryPacket}};


pub struct CarScreen {
    pub last_packet: Option<ControllerPacket>,
    pub car_yaw: f32,
    pub last_mpu_timestamp: u32,
    pub last_mpu_timestamp_sta : f64,
    pub car_pos: egui::Vec2,
    pub trajectory: VecDeque<[f64; 2]>,
    pub last_ky_timestamp: f64,
    pub last_dir: f32,
}

impl Default for CarScreen {
    fn default() -> Self {
        Self { 
            last_packet: None,
            car_yaw: 0.0,
            last_mpu_timestamp: 0,
            last_mpu_timestamp_sta : 0.0,
            car_pos: egui::Vec2::ZERO,
            trajectory: VecDeque::new(),
            last_ky_timestamp: 0.0,
            last_dir: 0.0,
        }
    }
}

impl CarScreen {
    pub fn show(&mut self, ctx: &egui::Context, screen: &mut ScreensTypes, data: &VecDeque<(TelemetryPacket, f64)>, 
        controller_connected: &Arc<AtomicBool>, rx_ctrl: &mpsc::Receiver<ControllerPacket>, start_instant: &Instant) 
    {

        // --- GETTING PACKETS ---
        while let Ok(pkt) = rx_ctrl.try_recv() {
            self.last_packet = Some(pkt);
        }

        let hc0_opt = data.iter().rev().find_map(|(p, _)| {
            if let TelemetryEnum::HCSR04(pck) = &p.packet { if pck.hc_id == 0 { Some((pck.get_distance_cm(), pck.blocked)) } else { None } } else { None }
        });

        let hc1_opt = data.iter().rev().find_map(|(p, _)| {
            if let TelemetryEnum::HCSR04(pck) = &p.packet { if pck.hc_id == 1 { Some((pck.get_distance_cm(), pck.blocked)) } else { None } } else { None }
        });

        let latest_hc0 = hc0_opt.unwrap_or((400.0, false));
        let latest_hc1 = hc1_opt.unwrap_or((400.0, false));

        let break_entry = data.iter().rev().find_map(|(p, _)| {
            if let TelemetryEnum::BREAK(pck) = &p.packet { Some((pck, p.hd_info.timestamp)) } else { None }
        });

        let mpu_entry = data.iter().rev().find_map(
            |(p, timestamp_sta)| {
            if let TelemetryEnum::MPU(pck) = &p.packet { 
                Some((pck, p.hd_info.timestamp, *timestamp_sta)) 
            } else { 
                None 
            }
        });

        let latest_bmp = data.iter().rev().find_map(
            |(p, _)| {
            if let TelemetryEnum::BMP(pck) = &p.packet { Some(pck) } else { None }
        });

        let latest_esp = data.iter().rev().find_map(|(p, _)| {
            if let TelemetryEnum::ESP(pck) = &p.packet { Some(pck) } else { None }
        });

        let latest_ping = data.iter().rev().find_map(|(p, _)| {
            if let TelemetryEnum::PONG(pck) = &p.packet { Some(pck) } else { None }
        });

        let latest_ina = data.iter().rev().find_map(|(p, _)| {
            if let TelemetryEnum::INA226(pck) = &p.packet { Some(pck) } else { None }
        });

        let ky_entry = data.iter().rev().find_map(|(p, ts)| {
            if let TelemetryEnum::KY033(pck) = &p.packet { Some((pck, *ts)) } else { None }
        });

        let latest_ky = ky_entry.map(|(k, _)| k); 
        
        // -- MPU integral (gyro->angle) --
        if let Some((mpu, current_ts_esp, current_ts_sta)) = mpu_entry {
            if current_ts_esp > self.last_mpu_timestamp {
                if self.last_mpu_timestamp > 0 {
                    // substract by preventing overflow
                    let diff_ms = current_ts_esp.saturating_sub(self.last_mpu_timestamp);
                    let dt = diff_ms as f32 / 1000.0;       
                    
                    if dt > 0.0 && dt < 1.0 {
                        let gyro_z_deg = mpu.get_gyro_deg_s().2 as f32;
                        
                        // angle = speed_gyro_z * dt
                        self.car_yaw += -gyro_z_deg.to_radians() * dt;
                        
                        // angle in [-pi,pi]
                        if self.car_yaw > std::f32::consts::PI { self.car_yaw -= 2.0 * std::f32::consts::PI; }
                        if self.car_yaw < -std::f32::consts::PI { self.car_yaw += 2.0 * std::f32::consts::PI; }
                    }
                }
                self.last_mpu_timestamp = current_ts_esp;
                self.last_mpu_timestamp_sta = current_ts_sta;
            }
        }

        // --- ENCODER (distance) ---
        if let Some((ky, current_ts)) = ky_entry {
            if current_ts > self.last_ky_timestamp {
                // Sign by ESP at the moment of the packet
                let dist = if ky.sign_motor_positive {ky.get_distance_m() as f32} else {-ky.get_distance_m() as f32};

                self.car_pos += egui::vec2(dist * self.car_yaw.sin(), -dist * self.car_yaw.cos());

                self.trajectory.push_back([self.car_pos.x as f64, self.car_pos.y as f64]);
                if self.trajectory.len() > 2000 { self.trajectory.pop_front(); }
                self.last_ky_timestamp = current_ts;
            }
        }

        //disconnected if not received mpu data for 500ms
        let is_ok = (start_instant.elapsed().as_secs_f64() - self.last_mpu_timestamp_sta) < 0.5;

        let sensors_connected = mpu_entry.is_some() && is_ok && hc0_opt.is_some() && hc1_opt.is_some();
        let connected = controller_connected.load(Ordering::Relaxed);

        // --- MAIN PANEL ---
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("RC Car Real-Time Control Center");
            ui.separator();

            // ==========================================
            // TOP BAND: ESP STATE
            // ==========================================
            if let Some(esp) = latest_esp {
                ui.group(|ui| {
                    ui.horizontal(|ui| {
                        // WIFI
                        let rssi_color = if esp.rssi > -60 { 
                            egui::Color32::GREEN 
                        } else if esp.rssi > -75 { 
                            egui::Color32::GOLD 
                        } else { 
                            egui::Color32::RED 
                        };
                        ui.label("Wi-fi RSSI :");
                        ui.colored_label(rssi_color, format!("{} dBm", esp.rssi));
                        ui.separator();

                        // TEMP SOC
                        let temp_color = if esp.esp_deg < 65.0 { 
                            egui::Color32::LIGHT_GREEN 
                        } else if esp.esp_deg < 80.0 { 
                            egui::Color32::GOLD 
                        } else { 
                            egui::Color32::RED 
                        };
                        ui.label("SOC Temp :");
                        ui.colored_label(temp_color, format!("{:.1} °C", esp.esp_deg));
                        ui.separator();

                        // Boot reason
                        ui.label(format!("Boot Reason : {}", esp.reset_reason.as_str()));
                        ui.separator();

                        // Number of control packets received by ESP
                        ui.label(format!("Packets Rx : {}", esp.nb_packets));

                        // Ping (RTT PC <-> ESP)
                        if let Some(ping) = latest_ping {
                            ui.separator();
                            let ping_color = if ping.ping_pong < 20 { 
                                egui::Color32::GREEN 
                            } else if ping.ping_pong < 60 { 
                                egui::Color32::GOLD 
                            } else { 
                                egui::Color32::RED 
                            };
                            ui.label("Ping :");
                            ui.colored_label(ping_color, format!("{} ms", ping.ping_pong));
                        }
                    });
                });
                ui.add_space(8.0);
            }

            ui.columns(3, |cols| {
                
                // ==========================================
                // COLUMN 1: CONTROLLER
                // ==========================================
                cols[0].vertical_centered(|ui| {
                    ui.label(egui::RichText::new("CONTROLLER INPUTS").strong());
                    ui.add_space(10.0);
                    
                    if connected {
                        if let Some(pkt) = &self.last_packet {
                            let (response, painter) = ui.allocate_painter(egui::vec2(200.0, 150.0), egui::Sense::hover());
                            let base = response.rect.min;
                            
                            painter.rect_filled(response.rect, 8.0, egui::Color32::from_gray(30));
                            
                            // Left joystick
                            let j_left_center = base + egui::vec2(50.0, 75.0);
                            painter.circle_stroke(j_left_center, 30.0, egui::Stroke::new(1.0, egui::Color32::GRAY));
                            let dx = (pkt.left_x as f32 / 100.0) * 25.0;
                            let dy = -(pkt.left_y as f32 / 100.0) * 25.0;
                            painter.circle_filled(j_left_center + egui::vec2(dx, dy), 10.0, egui::Color32::from_rgb(24, 95, 165));
                            
                            // Right joystick
                            let j_right_center = base + egui::vec2(150.0, 75.0);
                            painter.circle_stroke(j_right_center, 30.0, egui::Stroke::new(1.0, egui::Color32::GRAY));
                            let r_dx = (pkt.right_x as f32 / 100.0) * 25.0;
                            let r_dy = -(pkt.right_y as f32 / 100.0) * 25.0;
                            painter.circle_filled(j_right_center + egui::vec2(r_dx, r_dy), 10.0, egui::Color32::from_rgb(24, 95, 165));

                            ui.add_space(10.0);
                            ui.label(format!("Triggers - L: {}% | R: {}%", pkt.left_trig, pkt.right_trig));
                        } else {
                            ui.colored_label(egui::Color32::GRAY, "Waiting for inputs...");
                        }
                    } else {
                        // If no controller connected
                        ui.add_space(30.0);
                        ui.colored_label(egui::Color32::from_rgb(200, 50, 50), "Controller Disconnected");
                        ui.label("Connect via Bluetooth / USB");
                    }

                    // Small car with HC data
                    ui.label(egui::RichText::new("VEHICLE STATE").strong());
                    ui.add_space(10.0);

                    if sensors_connected {
                        let (response, painter) = ui.allocate_painter(egui::vec2(200.0, 200.0), egui::Sense::hover());
                        let center = response.rect.center();

                        let car_w = 40.0;
                        let car_h = 70.0;
                        
                        let direction = egui::vec2(self.car_yaw.sin(), -self.car_yaw.cos());
                        let side = egui::vec2(self.car_yaw.cos(), self.car_yaw.sin());

                        let p1 = center - direction * (car_h / 2.0) - side * (car_w / 2.0);
                        let p2 = center + direction * (car_h / 2.0) - side * (car_w / 2.0);
                        let p3 = center + direction * (car_h / 2.0) + side * (car_w / 2.0);
                        let p4 = center - direction * (car_h / 2.0) + side * (car_w / 2.0);

                        painter.add(egui::Shape::convex_polygon(
                            vec![p1, p2, p3, p4], 
                            egui::Color32::from_rgb(29, 158, 117), 
                            egui::Stroke::new(2.0, egui::Color32::WHITE)
                        ));
                        painter.line_segment([p2, p3], egui::Stroke::new(4.0, egui::Color32::YELLOW));

                        // Front HC
                        let front_color = if latest_hc0.0 < 20.0 { egui::Color32::RED } else if latest_hc0.0 < 50.0 { egui::Color32::GOLD } else { egui::Color32::GREEN };
                        let bar_front_y = center.y - 60.0;
                        painter.line_segment(
                            [egui::pos2(center.x - 50.0, bar_front_y), egui::pos2(center.x + 50.0, bar_front_y)],
                            egui::Stroke::new(5.0, front_color)
                        );
                        painter.text(
                            egui::pos2(center.x, bar_front_y - 12.0), 
                            egui::Align2::CENTER_CENTER, 
                            format!("{:.1} cm{}", latest_hc0.0, if latest_hc0.1 {", BLOCKED"} else {""}), 
                            egui::FontId::proportional(12.0), egui::Color32::WHITE
                        );

                        // Rear HC
                        let back_color = if latest_hc1.0 < 20.0 { egui::Color32::RED } else if latest_hc1.0 < 50.0 { egui::Color32::GOLD } else { egui::Color32::GREEN };
                        let bar_back_y = center.y + 60.0;
                        painter.line_segment(
                            [egui::pos2(center.x - 50.0, bar_back_y), egui::pos2(center.x + 50.0, bar_back_y)],
                            egui::Stroke::new(5.0, back_color)
                        );
                        painter.text(
                            egui::pos2(center.x, bar_back_y + 12.0), egui::Align2::CENTER_CENTER,
                            format!("{:.1} cm{}", latest_hc1.0, if latest_hc1.1 {", BLOCKED"} else {""}),
                            egui::FontId::proportional(12.0), 
                            egui::Color32::WHITE
                        );
                        
                    } else {
                        // If no telemetry
                        ui.add_space(40.0);
                        ui.colored_label(egui::Color32::GOLD, "Telemetry Offline");
                        ui.label("Missing required sensors data\n(HC-SR04, MPU)");
                    }

                    // Emergency brake state and monitoring
                    ui.label(egui::RichText::new("BRAKE STATE").strong());
                    ui.add_space(10.0);
                    if let Some((brake, _)) = break_entry {
                        ui.add_space(8.0);
                        
                        // Red if braking
                        let frame_bg = if brake.breaking { 
                            egui::Color32::from_rgb(60, 15, 15) 
                        } else { 
                            egui::Color32::from_gray(30) 
                        };

                        let stroke_color = if brake.breaking { 
                            egui::Color32::RED 
                        } else { 
                            egui::Color32::from_gray(60) 
                        };

                        egui::Frame::group(ui.style())
                            .fill(frame_bg)
                            .stroke(egui::Stroke::new(1.0, stroke_color))
                            .show(ui, |ui| {
                                ui.vertical_centered(|ui| {
                                    if brake.breaking {
                                        ui.label(egui::RichText::new(format!("BRAKE ACTIVE ({}ms)", brake.timeout_breaking)).strong().color(egui::Color32::RED));
                                    } else {
                                        ui.label(egui::RichText::new("BRAKE SYSTEM & PULSES").small().weak());
                                    }
                                });

                                ui.add_space(4.0);

                                ui.columns(3, |b_cols| {
                                    b_cols[0].vertical_centered(|ui| {
                                        ui.label(egui::RichText::new("PULSES 20ms").small().weak());
                                        ui.heading(format!("{}", brake.pulses_20ms));
                                    });
                                    b_cols[1].vertical_centered(|ui| {
                                        ui.label(egui::RichText::new("PULSES 100ms").small().weak());
                                        ui.heading(format!("{}", brake.pulses_100ms));
                                    });
                                    b_cols[2].vertical_centered(|ui| {
                                        ui.label(egui::RichText::new("PWR AT BRAKE").small().weak());
                                        ui.heading(format!("{}", brake.current_motor));
                                    });
                                });
                            });
                    }
                });

                // ==========================================
                // COLUMN 2 : PLOT OF CAR & OBSTACLES
                // ==========================================
                cols[1].vertical_centered(|ui| {

                    ui.add_space(10.0);
                    ui.horizontal(|ui| {
                        ui.label(egui::RichText::new("TRAJECTORY").small().weak());
                        if ui.small_button("Reset").clicked() {
                            self.trajectory.clear();
                            self.car_pos = egui::Vec2::ZERO;
                        }
                    });

                    let traj_vec: Vec<[f64; 2]> = self.trajectory.iter().cloned().collect();
                    Plot::new("car_trajectory")
                        .view_aspect(1.0)
                        .data_aspect(1.0)
                        .show(ui, |plot_ui| {
                            plot_ui.line(Line::new("trajectory", PlotPoints::from(traj_vec)).color(egui::Color32::from_rgb(24, 95, 165)));
                            plot_ui.points(
                                Points::new("position", vec![[self.car_pos.x as f64, self.car_pos.y as f64]])
                                    .radius(5.0)
                                    .color(egui::Color32::YELLOW),
                            );
                        });
                });

                // ==========================================
                // COLUMN 3 : TELEMETRY
                // ==========================================
                cols[2].vertical_centered(|ui| {
                    ui.group(|ui| {
                    ui.label("IMU values (Accel XY / Gyro Z)");
                    if let Some((mpu, _, _)) = mpu_entry {
                        let (ax, ay, _) = mpu.get_accel_g();
                        let gyro_z = -mpu.get_gyro_deg_s().2 as f32;

                        let (response, painter) = ui.allocate_painter(egui::vec2(160.0, 160.0), egui::Sense::hover());
                        let center = response.rect.center();
                        let radius = 60.0;

                        // Circle + cross of reference
                        painter.circle_stroke(center, radius, egui::Stroke::new(1.0, egui::Color32::from_gray(80)));
                        painter.line_segment([center - egui::vec2(radius, 0.0), center + egui::vec2(radius, 0.0)], egui::Stroke::new(1.0, egui::Color32::from_gray(60)));
                        painter.line_segment([center - egui::vec2(0.0, radius), center + egui::vec2(0.0, radius)], egui::Stroke::new(1.0, egui::Color32::from_gray(60)));

                        // Acceleration vector
                        let scale = radius / 2.0;
                        let accel_point = center + egui::vec2(ax.clamp(-2.0, 2.0) as f32 * scale, -ay.clamp(-2.0, 2.0) as f32 * scale);
                        painter.arrow(center, accel_point - center, egui::Stroke::new(3.0, egui::Color32::from_rgb(220, 60, 60)));
                        painter.circle_filled(accel_point, 4.0, egui::Color32::from_rgb(220, 60, 60));
                        painter.text(center + egui::vec2(0.0, radius + 14.0), egui::Align2::CENTER_CENTER,
                            format!("ax: {:.2}g  ay: {:.2}g", ax, ay), egui::FontId::proportional(11.0), egui::Color32::WHITE);

                        // Gyro Z: rotating needled
                        let gyro_clamped = gyro_z.clamp(-180.0, 180.0);
                        let gyro_angle_rad = (gyro_clamped / 180.0) * std::f32::consts::PI;
                        let needle_end = center + egui::vec2(gyro_angle_rad.sin(), -gyro_angle_rad.cos()) * (radius + 10.0);
                        painter.line_segment([center, needle_end], egui::Stroke::new(2.0, egui::Color32::from_rgb(80, 180, 255)));
                        painter.text(center + egui::vec2(0.0, radius + 28.0), egui::Align2::CENTER_CENTER,
                            format!("gyro Z: {:.1} °/s", gyro_z), egui::FontId::proportional(11.0), egui::Color32::from_rgb(80, 180, 255));
                    } else {
                        ui.colored_label(egui::Color32::GRAY, "No IMU data");
                    }
                });

                    ui.label(egui::RichText::new("TELEMETRY STATS").strong());
                    ui.add_space(10.0);

                    // Encoder speed
                    ui.group(|ui| {
                        ui.label("Speedometer (Pulses)");
                        if let Some(ky) = latest_ky {
                            ui.heading(format!("{} km/h", ky.get_speed_km_h()));
                        } else {
                            ui.heading("No Encoder data");
                        }
                        
                    });

                    // Battery
                    ui.group(|ui| {
                        ui.label("Battery Status");
                        if let Some(ina) = latest_ina {
                            let voltage = ina.get_bus_voltage_v();
                            let pct = ((voltage - 3.4) / (4.2 - 3.4)).clamp(0.0, 1.0) as f32;
                            
                            ui.add(egui::ProgressBar::new(pct).text(format!("{:.2} V", voltage)));
                            ui.label(format!("Current: {:.1} mA", ina.get_current_ma()));
                            ui.label(format!("Power: {:.1} mW", ina.get_power_mw()));
                        } else {
                            ui.colored_label(egui::Color32::GRAY, "No INA226 Data");
                        }
                    });

                    // ESP-S3 ressources
                    ui.group(|ui| {
                        ui.label("ESP32-S3 Hardware Resources");
                        if let Some(esp) = latest_esp {
                            // Core 0
                            ui.horizontal(|ui| {
                                ui.label(egui::RichText::new("Core 0:").small());
                                ui.add(egui::ProgressBar::new(esp.core0 / 100.0)
                                    .text(format!("{:.1}%", esp.core0)));
                            });
                            // Core 1
                            ui.horizontal(|ui| {
                                ui.label(egui::RichText::new("Core 1:").small());
                                ui.add(egui::ProgressBar::new(esp.core1 / 100.0)
                                    .text(format!("{:.1}%", esp.core1)));
                            });
                            
                            ui.separator();
                            
                            // DRAM memory
                            let dram_total = 320 * 1024;
                            let dram_used = dram_total - esp.free_dram;
                            ui.horizontal(|ui| {
                                ui.label(egui::RichText::new("DRAM:").small());
                                ui.add(egui::ProgressBar::new(dram_used as f32 / dram_total as f32)
                                    .text(format!("{:.0} KB left", esp.free_dram as f32 / 1024.0)));
                            });

                            // PSRAM memory (N16R8 ESP32-S3)
                            let psram_total = 8 * 1024 * 1024;
                            let psram_used = psram_total - esp.free_psram;
                            ui.horizontal(|ui| {
                                ui.label(egui::RichText::new("PSRAM:").small());
                                ui.add(egui::ProgressBar::new(psram_used as f32 / psram_total as f32)
                                    .text(format!("{:.2} MB left", esp.free_psram as f32 / (1024.0 * 1024.0))));
                            });
                        } else {
                            ui.colored_label(egui::Color32::GRAY, "No ESP32 Metrics Received");
                        }
                    });

                    // Environment (BMP)
                    ui.group(|ui| {
                        ui.label("Environment");
                        if let Some(bmp) = latest_bmp {
                            ui.label(format!("Temp: {:.1} °C", bmp.get_temperature_deg()));
                            ui.label(format!("Pressure: {:.2} bar", bmp.get_pressure_bar()));
                        } else {
                            ui.colored_label(egui::Color32::GRAY, "No Env Data");
                        }
                    });
                });
            });

            // Back button
            ui.with_layout(egui::Layout::bottom_up(egui::Align::LEFT), |ui| {
                if ui.button("Back").clicked() {
                    *screen = ScreensTypes::Main;
                }
            });
        });
    }
}