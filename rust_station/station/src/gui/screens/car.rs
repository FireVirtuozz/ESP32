use std::{collections::{HashMap, VecDeque}, dbg, f64::consts::{FRAC_PI_2, PI}, format, println, sync::{Arc, RwLock, atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering}, mpsc}, time::Instant};

use arc_swap::ArcSwap;
use egui_plot::{Line, Plot, PlotPoints, Points};
use image::load_from_memory;
use log::{debug, error, warn};
use nalgebra::Point2;

use crate::{config::AppConfig, control::{DriveCommand, MiscellaneousCommand, SafetyCommand}, controller::ControllerPacket, gui::{ScreensTypes, get_sensor_data}, odometry::{frames::{CAR_H, CAR_W}, odo::OdoState, slam::SlamState}, sensors::{SensorKey, TelemetryEnum, TelemetryPacket}};

fn load_car_icon(ctx: &egui::Context) -> egui::TextureHandle {
    let bytes = include_bytes!("../../../images/supra_v2.png"); // chemin relatif à CE fichier .rs
    let img = image::load_from_memory(bytes).expect("PNG voiture invalide");
    let rgba = img.to_rgba8();
    let size = [rgba.width() as usize, rgba.height() as usize];
    let color_image = egui::ColorImage::from_rgba_unmultiplied(size, rgba.as_flat_samples().as_slice());
    ctx.load_texture("car_icon", color_image, Default::default())
}

pub const ESP_CTRL_ID : u8 = 0;
pub const ESP_CAM_ID : u8 = 1;

pub struct CarScreen {
    pub last_packet: Option<ControllerPacket>,
    pub last_mpu_timestamp: u32,
    pub last_mpu_timestamp_sta : f64,
    pub last_dir: f32,
    pub texture: Option<egui::TextureHandle>,
    pub last_fps_time: std::time::Instant,
    pub car_icon: Option<egui::TextureHandle>,
    pub cam_texture: Option<egui::TextureHandle>,
    last_seen_version: u64,
    selected_esp_id: Option<u8>,
    pub current_history: VecDeque<f32>,
    pub last_slam: SlamState,
}

impl Default for CarScreen {
    fn default() -> Self {
        Self {
            last_packet: None,
            last_mpu_timestamp: 0,
            last_mpu_timestamp_sta : 0.0,
            last_dir: 0.0,
            texture: None,
            last_fps_time: Instant::now(),
            car_icon: None,
            cam_texture: None,
            last_seen_version: 0,
            selected_esp_id: None,
            current_history: VecDeque::with_capacity(100),
            last_slam: SlamState::default(),
        }
    }
}

impl CarScreen {
    pub fn show(&mut self, ctx: &egui::Context, 
        screen: &mut ScreensTypes, 
        devices: &HashMap<u8, [VecDeque<(TelemetryPacket, f64)>; SensorKey::COUNT]>, 
        controller_connected: &Arc<AtomicBool>, rx_ctrl: &mpsc::Receiver<ControllerPacket>,
        start_instant: &Instant, shared_texture: &Arc<RwLock<Option<egui::ColorImage>>>,
        shared_fps: &Arc<AtomicU32>, shared_frame_count: &Arc<AtomicU64>, 
        shared_odo: &Arc<RwLock<OdoState>>, shared_slam: &Arc<RwLock<SlamState>>, 
        packet_sent: &Arc<AtomicU32>, shared_live_scan: &Arc<RwLock<Vec<Point2<f32>>>>,
        active_drive: &Arc<ArcSwap<DriveCommand>>, active_safety: &Arc<ArcSwap<SafetyCommand>>,
        active_misc: &Arc<ArcSwap<MiscellaneousCommand>>,
        shared_waypoints: &Arc<RwLock<Vec<[f64; 2]>>>,
        shared_predicted_path: &Arc<RwLock<Vec<[f64; 2]>>>,
    ) {
        // --- GETTING PACKETS ---
        while let Ok(pkt) = rx_ctrl.try_recv() {
            self.last_packet = Some(pkt);
        }

        let (car_pose, trajectory, obstacles) = {
            let guard = shared_odo.read().unwrap();
            (guard.car_pose, guard.trajectory.clone(), guard.obstacles.clone())
        };

        let front_offset = CAR_H / 2.0; // tof at the front of the car

        if self.car_icon.is_none() {
            self.car_icon = Some(load_car_icon(ctx));
        }

        let hc0_opt = if let Some(buf) = 
        get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::HCSR04_0) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::HCSR04(hc), .. }, _)) = buf.back() {
                Some((hc.get_distance_cm(), hc.blocked))
            } else { None }
        } else { None };

        let hc1_opt = if let Some(buf) = 
        get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::HCSR04_1) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::HCSR04(hc), .. }, _)) = buf.back() {
                Some((hc.get_distance_cm(), hc.blocked))
            } else { None }
        } else { None };

        let latest_hc0 = hc0_opt.unwrap_or((400.0, false));
        let latest_hc1 = hc1_opt.unwrap_or((400.0, false));

        // --- BRAKE (Fixé sur `ESP_CTRL_ID`) ---
        let brake_entry = if let Some(buf) =
        get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::Brake) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::BRAKE(pck), hd_info }, timestamp_sta)) = buf.back() {
                Some((pck, hd_info.timestamp))
            } else { None }
        } else { None };

        // --- BNO085 ROTATION (Fixé sur `ESP_CTRL_ID`) ---
        let bno_entry_rot = if let Some(buf) = get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::Bno085Rotation) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::BNO085ROTATION(rot), .. }, timestamp_sta)) = buf.back() {
                self.last_mpu_timestamp_sta = *timestamp_sta;
                Some(rot)
            } else { None }
        } else { None };

        // --- BNO085 LINEAR ACCEL (Depuis l'ESP actif) ---
        let bno_entry_lin_accel = if let Some(buf) = get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::Bno085LinearAccel) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::BNO085LINEARACCEL(lin_accel), .. }, _)) = buf.back() {
                Some(lin_accel)
            } else { None }
        } else { None };

        // --- PONG ---
        let latest_ping = if let Some(buf) = get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::Pong) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::PONG(pck), .. }, _)) = buf.back() {
                Some(pck)
            } else { None }
        } else { None };

        // --- ADC BAT ---
        let latest_bat = if let Some(buf) = 
        get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::BatAdc) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::BATADC(pck), .. }, _)) = buf.back() {
                Some(pck)
            } else { None }
        } else { None };

        // --- ADC CURRENT ---
        if let Some(buf) = 
        get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::ACS758) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::ACS758(pck), .. }, _)) = buf.back() {
                let current_ma = pck.adc_current;
                self.current_history.push_back(current_ma);
                if self.current_history.len() > 100 {
                    self.current_history.pop_front();
                }
            }
        }

        // --- VL53L1X (ToF) ---
        let latest_tof = if let Some(buf) = get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::VL53L1X) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::VL53L1X(pck), .. }, _)) = buf.back() {
                if pck.status == 0 { Some(pck) } else { None }
            } else { None }
        } else { None };

        // --- KY033 ---
        let ky_entry = if let Some(buf) = get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::Ky033) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::KY033(pck), .. }, ts)) = buf.back() {
                Some((pck, *ts))
            } else { None }
        } else { None };
        let latest_ky = ky_entry.map(|(k, _)| k);

        let latest_gnss = if let Some(buf) = 
        get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::BZ181) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::BZ181(pck), .. }, _)) = buf.back() {
                Some(pck)
            } else { None }
        } else { None };

        // --- ESP ---
        let latest_esp_ctrl = if let Some(buf) = 
        get_sensor_data(devices, &Some(ESP_CTRL_ID), SensorKey::ESP) {
            if let Some((TelemetryPacket { packet: TelemetryEnum::ESP(pck), .. }, _)) = buf.back() {
                Some(pck)
            } else { None }
        } else { None };

        //disconnected if not received mpu data for 500ms
        let is_ok = (start_instant.elapsed().as_secs_f64() - self.last_mpu_timestamp_sta) < 0.5;

        let sensors_connected = bno_entry_rot.is_some() && bno_entry_lin_accel.is_some() 
            && is_ok && hc0_opt.is_some() && hc1_opt.is_some();
        let connected = controller_connected.load(Ordering::Relaxed);

        // --- MAIN PANEL ---
        egui::CentralPanel::default().show(ctx, |ui| {
            if ui.button("Back").clicked() {
                *screen = ScreensTypes::Main;
            }
            ui.heading("RC Car Real-Time Control Center");
            ui.separator();
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

                    ui.add_space(10.0);
                    ui.label(egui::RichText::new("MOTOR & STEERING").strong());
                    let ctrl_values = if let Some(esp) = latest_esp_ctrl {
                        &format!("Angle: {}, Motor: {}", esp.angle, esp.motor)
                    } else {
                        "No ESP data"
                    };
                    ui.label(format!("ESP: {}", ctrl_values));
                    let motor_control = active_drive.load();
                    ui.label(format!("Angle: {}, Motor: {}", motor_control.steering, motor_control.throttle));

                    ui.add_space(10.0);
                    ui.label(egui::RichText::new("SAFETY MEASURES").strong());
                    let safety_control = active_safety.load();
                    ui.label(format!("Emergency brake: {}, HC Blocking: {}", safety_control.brake_stop, safety_control.hc_block));

                    ui.add_space(10.0);
                    ui.label(egui::RichText::new("MISCELLANEOUS").strong());
                    let misc_control = active_misc.load();

                    ui.label(format!("Horn: {}, Drive Mode: {}, Target (m/s): {:.2}",
                        misc_control.horn, misc_control.drive_mode, misc_control.target_m_s));

                    ui.add_space(10.0);
                    // Small car with HC data
                    ui.label(egui::RichText::new("VEHICLE STATE").strong());
                    ui.add_space(10.0);

                    if sensors_connected {
                        let (response, painter) = ui.allocate_painter(egui::vec2(200.0, 200.0), egui::Sense::hover());
                        let center = response.rect.center();

                        let car_w = 40.0;
                        let car_h = 70.0;

                        let dir = car_pose.forward();
                        let s = car_pose.side();
                        let direction = egui::vec2(dir.x, dir.y);
                        let side = egui::vec2(s.x, s.y);

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
                            [egui::pos2(center.x - 50.0, bar_front_y), egui::pos2(center.x, bar_front_y)],
                            egui::Stroke::new(5.0, front_color)
                        );
                        painter.text(
                            egui::pos2(center.x - 50.0, bar_front_y - 12.0), 
                            egui::Align2::CENTER_CENTER, 
                            format!("{:.1} cm{}", latest_hc0.0, if latest_hc0.1 {", BLOCKED"} else {""}), 
                            egui::FontId::proportional(12.0), egui::Color32::WHITE
                        );

                        // Front ToF
                        let tof = if let Some(tof) = latest_tof {
                            tof.distance as f64 / 10.0
                        } else {
                            -1.0
                        };
                        let front_color = if tof < 20.0 { egui::Color32::RED } else if tof < 50.0 { egui::Color32::GOLD } else { egui::Color32::GREEN };
                        let bar_front_y = center.y - 60.0;
                        painter.line_segment(
                            [egui::pos2(center.x, bar_front_y), egui::pos2(center.x + 50.0, bar_front_y)],
                            egui::Stroke::new(5.0, front_color)
                        );
                        painter.text(
                            egui::pos2(center.x + 50.0, bar_front_y - 12.0), 
                            egui::Align2::CENTER_CENTER, 
                            format!("{:.1} cm", tof), 
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
                        ui.colored_label(egui::Color32::GOLD, "Telemetry Offline");
                        ui.label("Missing required sensors data\n(HC-SR04, MPU)");
                    }

                    // Emergency brake state and monitoring
                    ui.label(egui::RichText::new("BRAKE STATE").strong());
                    ui.add_space(10.0);
                    if let Some((brake, _)) = brake_entry {
                        ui.add_space(8.0);
                        
                        // Red if braking
                        let frame_bg = if brake.braking { 
                            egui::Color32::from_rgb(60, 15, 15) 
                        } else { 
                            egui::Color32::from_gray(30) 
                        };

                        let stroke_color = if brake.braking { 
                            egui::Color32::RED 
                        } else { 
                            egui::Color32::from_gray(60) 
                        };

                        egui::Frame::group(ui.style())
                            .fill(frame_bg)
                            .stroke(egui::Stroke::new(1.0, stroke_color))
                            .show(ui, |ui| {
                                ui.vertical_centered(|ui| {
                                    if brake.braking {
                                        ui.label(egui::RichText::new(format!("BRAKE ACTIVE ({}ms)", brake.timeout_braking)).strong().color(egui::Color32::RED));
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
                    // TODO: Arc<ColorImage> for perfs
                    let current_version = shared_frame_count.load(Ordering::Relaxed);
                    if current_version != self.last_seen_version {
                        let guard = shared_texture.read().unwrap();
                        if let Some(color_image) = guard.as_ref() {
                            match &mut self.cam_texture {
                                Some(tex) => tex.set(color_image.clone(), egui::TextureOptions::LINEAR),
                                None => {
                                    self.cam_texture = Some(ctx.load_texture(
                                        "camera_frame", color_image.clone(), egui::TextureOptions::LINEAR
                                    ));
                                }
                            }
                        }
                        self.last_seen_version = current_version;
                    }
                    if let Some(texture) = &self.cam_texture {
                        let response = ui.image(&*texture);
                        let fps = shared_fps.load(Ordering::Relaxed) as f32 / 10.0;
                        let painter = ui.painter_at(response.rect);
                        painter.text(
                            response.rect.left_top() + egui::vec2(10.0, 10.0),
                            egui::Align2::LEFT_TOP,
                            format!("{:.1} FPS", fps),
                            egui::FontId::proportional(16.0),
                            if fps < 15.0 { egui::Color32::RED } else { egui::Color32::GREEN },
                        );
                    } else {
                        ui.centered_and_justified(|ui| {
                            ui.label("Camera not connected");
                        });
                    }

                    ui.add_space(10.0);

                    if let Ok(s) = shared_slam.try_read() { self.last_slam = s.clone(); }
                    let slam_state = &self.last_slam;

                    Plot::new("lidar_live_plot")
                        .data_aspect(1.0) // Keeps 1:1 scale so meters aren't distorted
                        .width(ui.available_width())
                        .height(ui.available_height()) // Fills the available space in the tab/panel
                        .default_x_bounds(-5.0, 5.0)
                        .default_y_bounds(-5.0, 5.0)
                        .show(ui, |plot_ui| {
                            plot_ui.points(Points::new("slam_map", slam_state.cached_grid_points.clone())
                                        .radius(1.5).color(egui::Color32::LIGHT_BLUE));

                            let live: PlotPoints = slam_state.map_points.iter()
                                .map(|p| [p.x as f64, p.y as f64]).collect();
                            plot_ui.points(Points::new("live_scan", live).radius(2.0).color(egui::Color32::RED));

                            let (px, py, yaw) = slam_state.corrected_pose;
                            let (px, py) = (px as f64, py as f64);
                            let tip = [px + 0.3 * (yaw as f64).cos(), py + 0.3 * (yaw as f64).sin()];
                            plot_ui.line(Line::new("heading", vec![[px, py], tip]).color(egui::Color32::GREEN).width(2.0));
                            if let Some(icon) = &self.car_icon {
                                plot_ui.image(
                                    egui_plot::PlotImage::new(
                                        "car",
                                        icon.id(),
                                        egui_plot::PlotPoint::new(px, py),
                                        egui::vec2(CAR_W as f32, CAR_H as f32),
                                    )
                                    .rotate(yaw as f64 - FRAC_PI_2),
                                );
                            }
                            //plot_ui.points(Points::new("car", vec![[px, py]]).radius(5.0).color(egui::Color32::GREEN));

                            // 3. Origin marker
                            plot_ui.points(
                                Points::new("origin", vec![[0.0, 0.0]])
                                    .radius(4.0)
                                    .color(egui::Color32::WHITE)
                            );

                            if plot_ui.response().clicked_by(egui::PointerButton::Secondary) {
                                if let Some(pos) = plot_ui.pointer_coordinate() {
                                    let mut waypoints = shared_waypoints.write().unwrap();
                                    waypoints.push([pos.x, pos.y]);
                                }
                            }

                            let waypoints = shared_waypoints.read().unwrap();
                            if !waypoints.is_empty() {
                                let wp_plot_points: PlotPoints = waypoints.iter().cloned().collect();
                                plot_ui.points(Points::new("waypoints", wp_plot_points).radius(5.0).color(egui::Color32::YELLOW));
                                
                                // Ligne reliant la voiture aux waypoints (optionnel pour visualiser le chemin)
                                let mut path_line = vec![[px, py]];
                                path_line.extend(waypoints.iter().cloned());
                                plot_ui.line(Line::new("target_path", path_line).color(egui::Color32::YELLOW).width(2.0));
                            }

                            let path_guard = shared_predicted_path.read().unwrap();
                            let predicted_line = Line::new("predicted_path", path_guard.clone())
                                .color(egui::Color32::from_rgb(255, 165, 0))
                                .width(3.0);

                            plot_ui.line(predicted_line);
                        });
                });

                // ==========================================
                // COLUMN 3 : TELEMETRY
                // ==========================================
                cols[2].vertical_centered(|ui| {
                    ui.group(|ui| {
                    ui.label("IMU values (Accel XY / Gyro Z)");
                    if let (Some(bno_lin_accel), Some(bno_rot)) 
                    = (bno_entry_lin_accel, bno_entry_rot) {
                        let (ax, ay, _) = bno_lin_accel.get_accel_g();
                        let gyro_z = -bno_rot.get_angles_deg().1 as f32; //pitch as yaw

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
                            format!("gyro Z: {:.1} °", gyro_z), egui::FontId::proportional(11.0), egui::Color32::from_rgb(80, 180, 255));
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
                        if let Some(bat) = latest_bat {
                            let voltage = bat.adc_voltage;
                            let pct = ((voltage - 7.2) / (8.1 - 7.2)).clamp(0.0, 1.0);
                            
                            ui.add(egui::ProgressBar::new(pct).text(format!("{:.2} V", voltage)));

                            ui.add_space(4.0);

                        } else {
                            ui.colored_label(egui::Color32::GRAY, "No Battery Data");
                        }

                        ui.label("Current Status");

                        if self.current_history.len() > 1 {
                            // --- SPARKLINE MINI-PLOT ---
                            let desired_size = egui::vec2(ui.available_width(), 35.0);
                            let (response, painter) = ui.allocate_painter(desired_size, egui::Sense::hover());
                            let rect = response.rect;

                            painter.rect_filled(rect, 3.0, egui::Color32::from_gray(25));

                            let max_val = self.current_history.iter().cloned().fold(100.0f32, f32::max);
                            let min_val = 0.0f32;
                            let range = (max_val - min_val).max(1.0);

                            let points: Vec<egui::Pos2> = self.current_history
                                .iter()
                                .enumerate()
                                .map(|(i, &val)| {
                                    let x = rect.left() + (i as f32 / (self.current_history.len() - 1) as f32) * rect.width();
                                    let norm = ((val - min_val) / range).clamp(0.0, 1.0);
                                    let y = rect.bottom() - norm * rect.height();
                                    egui::pos2(x, y)
                                })
                                .collect();

                            painter.add(egui::Shape::line(
                                points, egui::Stroke::new(1.5, egui::Color32::from_rgb(80, 200, 120))
                            ));

                            if let Some(&latest_val) = self.current_history.back() {
                                let text = format!("{:.1} A", latest_val);
                                
                                painter.text(
                                    rect.right_top() + egui::vec2(-6.0, 4.0),
                                    egui::Align2::RIGHT_TOP,
                                    text,
                                    egui::FontId::proportional(11.0),
                                    egui::Color32::WHITE,
                                );
                            }

                        } else {
                            ui.colored_label(egui::Color32::GRAY, "No Current Data");
                        }
                    });

                    // ESP-S3 ressources
                    ui.group(|ui| {
                        ui.label("ESP32-S3 Hardware Resources");
                        ui.horizontal(|ui| {
                            ui.strong("Active ESP :");
                            let mut ids: Vec<u8> = devices.keys().cloned().collect();
                            ids.sort();
                            for id in ids {
                                let is_selected = self.selected_esp_id == Some(id);
                                if ui.selectable_label(is_selected, format!("ESP #{}", id)).clicked() {
                                    self.selected_esp_id = Some(id);
                                }
                            }
                        });
                        ui.separator();

                        // --- ESP ---
                        let latest_esp = if let Some(buf) = 
                        get_sensor_data(devices, &self.selected_esp_id, SensorKey::ESP) {
                            if let Some((TelemetryPacket { packet: TelemetryEnum::ESP(pck), .. }, _)) = buf.back() {
                                Some(pck)
                            } else { None }
                        } else { None };

                        if let Some(esp) = latest_esp {
                            ui.group(|ui| {
                                ui.set_width(ui.available_width()); // force le group à occuper toute la largeur dispo, pas sa largeur "naturelle"

                                // Ligne 1 : coupe en deux horizontales au lieu d'une seule qui s'étire
                                ui.horizontal_wrapped(|ui| { // wrap automatique si ça dépasse, au lieu de forcer un seul scroll horizontal invisible
                                    let rssi_color = if esp.rssi > -60 { egui::Color32::GREEN } else if esp.rssi > -75 { egui::Color32::GOLD } else { egui::Color32::RED };
                                    ui.label("Wi-Fi:");
                                    ui.colored_label(rssi_color, format!("{}dB", esp.rssi));
                                    ui.separator();

                                    let temp_color = if esp.esp_deg < 65.0 { egui::Color32::LIGHT_GREEN } else if esp.esp_deg < 80.0 { egui::Color32::GOLD } else { egui::Color32::RED };
                                    ui.label("SOC:");
                                    ui.colored_label(temp_color, format!("{:.0}°C", esp.esp_deg));
                                    ui.separator();

                                    ui.label(format!("Boot: {}", esp.reset_reason.as_str()));
                                    ui.separator();

                                    let packets_diff = packet_sent.load(Ordering::Relaxed).saturating_sub(esp.nb_packets);
                                    ui.label(format!("Lost: {}", packets_diff));

                                    ui.separator();
                                    
                                    let ping_pong = if let Some(ping) = latest_ping {ping.ping_pong} else {0};
                                    let ping_color = if ping_pong < 20 { egui::Color32::GREEN } else if ping_pong < 60 { egui::Color32::GOLD } else { egui::Color32::RED };
                                    ui.label("Ping:");
                                    ui.colored_label(ping_color, format!("{}ms", ping_pong));

                                });

                                ui.add_space(4.0);

                                // Ligne 2 : CPUs — empile verticalement plutôt que côte à côte,
                                // chaque barre prend toute la largeur dispo (plus de dépassement possible)
                                ui.add(egui::ProgressBar::new(esp.core0 / 100.0).text(format!("Core 0: {:.1}%", esp.core0)));
                                ui.add(egui::ProgressBar::new(esp.core1 / 100.0).text(format!("Core 1: {:.1}%", esp.core1)));

                                ui.add_space(4.0);

                                // Ligne 3 : Mémoires, même principe — vertical, pleine largeur
                                let dram_used = (320 * 1024) - esp.free_dram;
                                ui.add(egui::ProgressBar::new(dram_used as f32 / (320.0 * 1024.0))
                                    .text(format!("DRAM: {:.0} KB free", esp.free_dram as f32 / 1024.0)));

                                let psram_used = (8 * 1024 * 1024) - esp.free_psram;
                                ui.add(egui::ProgressBar::new(psram_used as f32 / (8.0 * 1024.0 * 1024.0))
                                    .text(format!("PSRAM: {:.2} MB free", esp.free_psram as f32 / (1024.0 * 1024.0))));
                            });
                        } else {
                            ui.colored_label(egui::Color32::GRAY, "No ESP32 Metrics Received");
                        }
                    });

                    // --- GNSS BZ-181 STATUS ---
                    ui.group(|ui| {
                        ui.label(egui::RichText::new("GNSS (BZ-181)").strong());
                        if let Some(gnss) = latest_gnss {
                            let fix_str = if gnss.status == b'A' { "Active (3D/2D)" } else { "Void" };
                            ui.label(format!("Fix : {} | Sats : {}", fix_str, gnss.satellites_used));
                            ui.label(format!("Lat/Lon : {:.5}, {:.5}", gnss.latitude, gnss.longitude));
                            ui.label(format!("Speed : {:.1} km/h | HDOP : {:.2}", gnss.speed_kmh, gnss.hdop));
                        } else {
                            ui.colored_label(egui::Color32::GRAY, "No GNSS Data");
                        }
                    });
                });
            });
        
        });
    }
}