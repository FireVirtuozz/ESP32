use std::{collections::{HashMap, VecDeque}, fmt::format, format, sync::{Arc, RwLock, atomic::{AtomicU32, Ordering}}};

use egui::Vec2b;
use egui_plot::{Line, Plot, PlotBounds, PlotPoint, PlotPoints, Points, Text};
use log::error;
use nalgebra::Point2;

use crate::{gui::{MyApp, ScreensTypes, get_sensor_data}, odometry::{ProcessedKey, ProcessedTelemetry, slam::SlamState}, sensors::{SensorKey, TelemetryEnum, TelemetryPacket, lidar::LidarDiagnostics}};

#[derive(PartialEq)]
pub enum MpuSubTab {
    Acceleration,
    Gyroscope,
    Temperature,
    Processed,
}

#[derive(PartialEq)]
pub enum BnoSubTab {
    Acceleration,
    LinearAcceleration,
    Rotation,
}

#[derive(PartialEq, Copy, Clone)]
pub enum SensorTab {
    HCSR04, // Ultrasonic
    INA226, // Voltage & current monitoring
    MPU, //IMU
    BMP280, // Pressure & Temperature
    KY003, // Linear Hall
    RFIDRC522, // Rfid car reader
    RCWL0515, // Radar doppler
    KY033, // Color tracking sensor 
    ESP,
    DHT11, // Humidity & Temperature
    PHOTOSENSOR, // KY018
    VL53L1X,
    BNO085,
    BATADC,
    ACS758,
    LIDAR,
    BZ181,
}

pub struct SensorsScreen {
    pub sensor_selected : Option<SensorTab>,
    pub mpu_sub_tab: MpuSubTab,
    pub bno_sub_tab: BnoSubTab,
    pub selected_esp_id: Option<u8>,
}

impl Default for SensorsScreen {
    fn default() -> Self {
        Self {
            sensor_selected : None,
            mpu_sub_tab: MpuSubTab::Acceleration,
            bno_sub_tab: BnoSubTab::Acceleration,
            selected_esp_id: None,
        }
    }
}

impl SensorsScreen {

    const TIME_WINDOW: f64 = 30.0;

    fn make_plot(ui: &mut egui::Ui,
        name: &str,
        y_label: &str,
        y_range: Option<(f64, f64)>,
        points: PlotPoints,
        min_t: f64
    ) {
                            
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
                let mut latest_t = 0.0;
                if let Some(last_pt) = points.points().last() {
                    let current_y = last_pt.y;
                    latest_t = last_pt.x;
                    
                    plot_ui.text(
                        Text::new(
                            name, 
                            PlotPoint::new(latest_t, current_y), 
                            format!("  {:.2}", current_y)
                        )
                        .anchor(egui::Align2::RIGHT_TOP)
                        .color(egui::Color32::WHITE)
                    );
                }
                plot_ui.line(Line::new(name, points).width(2.5));
                let (y_min, y_max) = y_range.unwrap_or((0.0, 0.0));

                plot_ui.set_plot_bounds(PlotBounds::from_min_max(
                    [min_t, y_min],
                    [latest_t.max(SensorsScreen::TIME_WINDOW), y_max],
                ));

                let auto_y = y_range.is_none();
                plot_ui.set_auto_bounds(Vec2b::new(false, auto_y));
            });
    }

    fn render_processed_plot<F>(
        &self,
        ui: &mut egui::Ui,
        data: &[VecDeque<(ProcessedTelemetry, f64)>], // Ajuste le type selon ta structure
        sensor_key: ProcessedKey,
        name: &str,
        y_label: &str,
        y_range: Option<(f64, f64)>,
        extract_val: F,
    ) where
        F: Fn(&ProcessedTelemetry) -> Option<f64>,
    {
        let sensor_data = &data[sensor_key.index()];

        if let Some((_, latest_t)) = sensor_data.back() {
            let min_t = *latest_t - SensorsScreen::TIME_WINDOW;

            let points: PlotPoints = sensor_data
                .iter()
                .skip_while(|(_, t)| *t < min_t)
                .filter_map(|(p, t)| extract_val(&p).map(|val| [*t, val]))
                .collect();

            SensorsScreen::make_plot(ui, name, y_label, y_range, points, min_t);
        } else {
            ui.weak(format!("No valid data for {}", name));
        }
    }

    fn render_sensor_plot<F>(
        &self,
        ui: &mut egui::Ui,
        data: &[VecDeque<(TelemetryPacket, f64)>; SensorKey::COUNT],
        sensor_key: SensorKey,
        name: &str,
        y_label: &str,
        y_range: Option<(f64, f64)>,
        extract_val: F,
    ) where
        F: Fn(&TelemetryEnum) -> Option<f64>,
    {
        let sensor_data = &data[sensor_key.index()];
        if let Some((_, latest_t)) = sensor_data.back() {
            let min_t = *latest_t - SensorsScreen::TIME_WINDOW;

            let points: PlotPoints = sensor_data
                .iter()
                .skip_while(|(_, t)| *t < min_t)
                .filter_map(|(p, t)| extract_val(&p.packet).map(|val| [*t, val]))
                .collect();

            SensorsScreen::make_plot(ui, name, y_label, y_range, points, min_t);
        } else {
            ui.weak(format!("No valid data for {}", name));
        }
    }

    pub fn show(&mut self, ctx: &egui::Context, 
        devices: &HashMap<u8, [VecDeque<(TelemetryPacket, f64)>; SensorKey::COUNT]>,
        screen : &mut ScreensTypes, data_processed: &[VecDeque<(ProcessedTelemetry, f64)>; ProcessedKey::COUNT],
        packet_sent: &Arc<AtomicU32>, shared_live_scan: &Arc<RwLock<Vec<Point2<f32>>>>,
        shared_lidar_diag: &Arc<RwLock<LidarDiagnostics>>, shared_slam: &Arc<RwLock<SlamState>>
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

        egui::TopBottomPanel::top("sensors_filter").show(ctx, |ui| {
            ui.horizontal(|ui| {
                if ui.button("Back").clicked() { *screen = ScreensTypes::Main; }
                ui.label("Active sensor :"); 

                // None button
                if ui.selectable_label(self.sensor_selected.is_none(), "None").clicked() {
                    self.sensor_selected = None;
                }

                ui.separator();

                const SENSORS: &[(SensorTab, &str)] = &[
                    (SensorTab::HCSR04, "HC-SR04"),
                    (SensorTab::INA226, "INA226"),
                    (SensorTab::MPU, "MPU"),
                    (SensorTab::BMP280, "BMP280"),
                    (SensorTab::KY003, "KY-003"),
                    (SensorTab::RFIDRC522, "RFID-RC522"),
                    (SensorTab::RCWL0515, "RCWL-0515"),
                    (SensorTab::KY033, "KY-033"),
                    (SensorTab::ESP, "ESP"),
                    (SensorTab::DHT11, "DHT-11"),
                    (SensorTab::PHOTOSENSOR, "LIGHT"),
                    (SensorTab::VL53L1X, "VL53L1X"),
                    (SensorTab::BNO085, "BNO085"),
                    (SensorTab::BATADC, "BAT_ADC"),
                    (SensorTab::ACS758, "ACS758"),
                    (SensorTab::LIDAR, "LD500"),
                    (SensorTab::BZ181, "BZ181"),
                ];

                for &(tab, label) in SENSORS {
                    let is_selected = self.sensor_selected == Some(tab);
                    if ui.selectable_label(is_selected, label).clicked() {
                        self.sensor_selected = Some(tab);
                    }
                }
            });
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                if let Some(active_id) = self.selected_esp_id {
                    if let Some(data) = devices.get(&active_id) {
                        match self.sensor_selected {
                            Some(SensorTab::HCSR04) => {
                                ui.heading("HC-SR04 - Ultrasonic sensor");

                                let get_dist = |pck: &TelemetryEnum| match pck {
                                    TelemetryEnum::HCSR04(p) => Some(p.get_distance_cm()),
                                    _ => None,
                                };

                                self.render_sensor_plot(
                                    ui, data, SensorKey::HCSR04_0,
                                    "HC-SR04_DISTANCE [0]", "Distance (cm)",
                                    Some((0.0, 600.0)), get_dist,
                                );

                                self.render_sensor_plot(
                                    ui, data, SensorKey::HCSR04_1,
                                    "HC-SR04_DISTANCE [1]", "Distance (cm)",
                                    Some((0.0, 600.0)), get_dist,
                                );
                            },
                            Some(SensorTab::INA226) => {
                                ui.heading("INA226 - Electrical monitoring");

                                self.render_sensor_plot(
                                    ui, data, SensorKey::INA226,
                                    "INA_BUS_VOLTAGE", "Bus voltage (V)",
                                    Some((-6.0, 6.0)),
                                    |pck| match pck {
                                        TelemetryEnum::INA226(p) => Some(p.get_bus_voltage_v()),
                                        _ => None,
                                    },
                                );

                                self.render_sensor_plot(
                                    ui, data, SensorKey::INA226,
                                    "INA_SHUNT_VOLTAGE", "Shunt voltage (mV)",
                                    Some((-2.0, 2.0)),
                                    |pck| match pck {
                                        TelemetryEnum::INA226(p) => Some(p.get_shunt_voltage_mv()),
                                        _ => None,
                                    },
                                );

                                self.render_sensor_plot(
                                    ui, data, SensorKey::INA226,
                                    "INA_CURRENT", "Current (mA)",
                                    Some((0.0, 30.0)),
                                    |pck| match pck {
                                        TelemetryEnum::INA226(p) => Some(p.get_current_ma()),
                                        _ => None,
                                    },
                                );

                                self.render_sensor_plot(
                                    ui, data, SensorKey::INA226,
                                    "INA_POWER", "Power (mW)",
                                    Some((0.0, 50.0)),
                                    |pck| match pck {
                                        TelemetryEnum::INA226(p) => Some(p.get_power_mw()),
                                        _ => None,
                                    },
                                );
                            },
                            Some(SensorTab::KY003) => {
                                ui.heading("KY-003 - Hall sensor");

                                self.render_sensor_plot(
                                    ui, data, SensorKey::KY003,
                                    "KY_SPEED", "Speed (km/h)",
                                    Some((0.0, 100.0)),
                                    |pck| match pck {
                                        TelemetryEnum::KY003(p) => p.get_speed_km_h().ok(),
                                        _ => None,
                                    },
                                );

                                self.render_sensor_plot(
                                    ui, data, SensorKey::KY003,
                                    "KY_DISTANCE", "Cumulated distance (m)",
                                    None,
                                    |pck| match pck {
                                        TelemetryEnum::KY003(p) => Some(p.get_distance_m()),
                                        _ => None,
                                    },
                                );
                            },
                            Some(SensorTab::DHT11) => {
                                ui.heading("DHT-11: Temperature and Humidity");

                                self.render_sensor_plot(
                                    ui, data, SensorKey::DHT11,
                                    "TEMPERATURE", "Temperature (°C)",
                                    Some((0.0, 50.0)),
                                    |pck| match pck {
                                        TelemetryEnum::DHT11(p) => Some(p.temperature as f64),
                                        _ => None,
                                    },
                                );

                                self.render_sensor_plot(
                                    ui, data, SensorKey::DHT11,
                                    "HUMIDITY", "Humidity (%)",
                                    Some((0.0, 100.0)),
                                    |pck| match pck {
                                        TelemetryEnum::DHT11(p) => Some(p.humidity as f64),
                                        _ => None,
                                    },
                                );
                            },
                            Some(SensorTab::PHOTOSENSOR) => {
                                ui.heading("KY018: Photosensor");

                                self.render_sensor_plot(
                                    ui, data, SensorKey::Photosensor,
                                    "LIGHT", "Light raw value",
                                    Some((0.0, 200.0)),
                                    |pck| match pck {
                                        TelemetryEnum::PHOTOSENSOR(p) => Some(p.raw_value as f64),
                                        _ => None,
                                    },
                                );
                            },
                            Some(SensorTab::VL53L1X) => {
                                ui.heading("VL53L1X: ToF sensor");

                                let sensor_data = &data[SensorKey::VL53L1X.index()];
                                let status_str = sensor_data.iter().rev().find_map(|(pck, _)| {
                                    if let TelemetryEnum::VL53L1X(vl) = &pck.packet {
                                        Some(format!("{}", vl.status))
                                    } else {
                                        None
                                    }
                                }).unwrap_or_else(|| "No data found".to_string());

                                ui.label(status_str);

                                ui.label("Distance")
                                    .on_hover_text("Distance measured between sensor and target");
                                self.render_sensor_plot(
                                    ui, data, SensorKey::VL53L1X,
                                    "Distance", "Distance (mm)",
                                    Some((0.0, 2000.0)),
                                    |pck| match pck {
                                        TelemetryEnum::VL53L1X(p) if p.status == 0 => Some(p.distance as f64),
                                        _ => None,
                                    },
                                );

                                ui.label("Ambient light")
                                    .on_hover_text("Ambient light noise in kcps (kilo counts per second = thousands of photons/s).\nThe more it is, the higher the range is");
                                self.render_sensor_plot(
                                    ui, data, SensorKey::VL53L1X,
                                    "Ambient", "Ambient (kcps)",
                                    Some((0.0, 2000.0)),
                                    |pck| match pck {
                                        TelemetryEnum::VL53L1X(p) if p.status == 0 => Some(p.ambient as f64),
                                        _ => None,
                                    },
                                );

                                ui.label("Signal per SPAD")
                                    .on_hover_text("Power of return laser received by photodiode (kcps/SPAD).\nDepends on reflection of target and related to measure reliability");
                                self.render_sensor_plot(
                                    ui, data, SensorKey::VL53L1X,
                                    "Signal per SPAD", "sig_per_spad (kcps/SPAD)",
                                    Some((0.0, 60000.0)),
                                    |pck| match pck {
                                        TelemetryEnum::VL53L1X(p) if p.status == 0 => Some(p.sig_per_spad as f64),
                                        _ => None,
                                    },
                                );

                                ui.label("Active SPADs")
                                    .on_hover_text("SPAD (Single Photon Avalanche Diode) : sensitive photodiodes that fills the sensor matrix.\nShows how many photodiodes are activated in the ROI");
                                self.render_sensor_plot(
                                    ui, data, SensorKey::VL53L1X,
                                    "Number of SPADs", "Active SPADs (?)",
                                    Some((0.0, 256.0)),
                                    |pck| match pck {
                                        TelemetryEnum::VL53L1X(p) if p.status == 0 => Some(p.num_spad as f64),
                                        _ => None,
                                    },
                                );
                            },
                            Some(SensorTab::MPU) => {
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
                                    if ui.selectable_label(self.mpu_sub_tab == MpuSubTab::Processed, "Processed").clicked() {
                                        self.mpu_sub_tab = MpuSubTab::Processed;
                                    }
                                });
                                ui.separator();

                                match self.mpu_sub_tab {
                                    MpuSubTab::Acceleration => {
                                        self.render_sensor_plot(
                                            ui, data, SensorKey::MPU,
                                            "MPU_ACCEL_X", "Acceleration X (g)",
                                            Some((-3.0, 3.0)),
                                            |pck| match pck {
                                                TelemetryEnum::MPU(p) => Some(p.get_accel_g().0),
                                                _ => None,
                                            },
                                        );

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::MPU,
                                            "MPU_ACCEL_Y", "Acceleration Y (g)",
                                            Some((-3.0, 3.0)),
                                            |pck| match pck {
                                                TelemetryEnum::MPU(p) => Some(p.get_accel_g().1),
                                                _ => None,
                                            },
                                        );

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::MPU,
                                            "MPU_ACCEL_Z", "Acceleration Z (g)",
                                            Some((-3.0, 3.0)),
                                            |pck| match pck {
                                                TelemetryEnum::MPU(p) => Some(p.get_accel_g().2),
                                                _ => None,
                                            },
                                        );
                                    },
                                    MpuSubTab::Gyroscope => {
                                        self.render_sensor_plot(
                                            ui, data, SensorKey::MPU,
                                            "MPU_GYRO_X", "Gyro X (°/s)",
                                            Some((-300.0, 300.0)),
                                            |pck| match pck {
                                                TelemetryEnum::MPU(p) => Some(p.get_gyro_deg_s().0),
                                                _ => None,
                                            },
                                        );

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::MPU,
                                            "MPU_GYRO_Y", "Gyro Y (°/s)",
                                            Some((-300.0, 300.0)),
                                            |pck| match pck {
                                                TelemetryEnum::MPU(p) => Some(p.get_gyro_deg_s().1),
                                                _ => None,
                                            },
                                        );

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::MPU,
                                            "MPU_GYRO_Z", "Gyro Z (°/s)",
                                            Some((-300.0, 300.0)),
                                            |pck| match pck {
                                                TelemetryEnum::MPU(p) => Some(p.get_gyro_deg_s().2),
                                                _ => None,
                                            },
                                        );
                                    },
                                    MpuSubTab::Temperature => {
                                        self.render_sensor_plot(
                                            ui, data, SensorKey::MPU,
                                            "MPU_TEMP", "MPU Temperature (°C)",
                                            Some((0.0, 50.0)),
                                            |pck| match pck {
                                                TelemetryEnum::MPU(p) => Some(p.get_temperature_chip_deg()),
                                                _ => None,
                                            },
                                        );
                                    },
                                    MpuSubTab::Processed => {
                                        self.render_processed_plot(
                                            ui, data_processed, ProcessedKey::MPU,
                                            "MPU_PROC_ACCEL_X", "Acceleration X (g)",
                                            Some((-3.0, 3.0)),
                                            |pck| match pck {
                                                ProcessedTelemetry::MPU(p) => Some(p.ax_filt),
                                                _ => None,
                                            },
                                        );

                                        self.render_processed_plot(
                                            ui, data_processed, ProcessedKey::MPU,
                                            "MPU_PROC_ACCEL_Y", "Acceleration Y (g)",
                                            Some((-3.0, 3.0)),
                                            |pck| match pck {
                                                ProcessedTelemetry::MPU(p) => Some(p.ay_filt),
                                                _ => None,
                                            },
                                        );

                                        self.render_processed_plot(
                                            ui, data_processed, ProcessedKey::MPU,
                                            "MPU_PROC_VEL_X", "Velocity X (m/s)",
                                            Some((-5.0, 5.0)),
                                            |pck| match pck {
                                                ProcessedTelemetry::MPU(p) => Some(p.vel_x),
                                                _ => None,
                                            },
                                        );

                                        self.render_processed_plot(
                                            ui, data_processed, ProcessedKey::MPU,
                                            "MPU_PROC_VEL_Y", "Velocity Y (m/s)",
                                            Some((-5.0, 5.0)),
                                            |pck| match pck {
                                                ProcessedTelemetry::MPU(p) => Some(p.vel_y),
                                                _ => None,
                                            },
                                        );
                                    }
                                }
                            },
                            Some(SensorTab::BNO085) => {
                                ui.heading("BNO085 - Inertial measurement unit (IMU)");

                                let classif_data = data[SensorKey::Bno085Classif.index()].back();
                                let accel_data = data[SensorKey::Bno085Accel.index()].back();
                                let linear_accel_data = data[SensorKey::Bno085LinearAccel.index()].back();
                                let rotation_data = data[SensorKey::Bno085Rotation.index()].back();

                                ui.horizontal(|ui| {
                                    ui.label("Data :");
                                    if ui.selectable_label(self.bno_sub_tab == BnoSubTab::Acceleration,
                                        "Acceleration").clicked() {
                                        self.bno_sub_tab = BnoSubTab::Acceleration;
                                    }
                                    if ui.selectable_label(self.bno_sub_tab == BnoSubTab::Rotation,
                                        "Rotation").clicked() {
                                        self.bno_sub_tab = BnoSubTab::Rotation;
                                    }
                                    if ui.selectable_label(self.bno_sub_tab == BnoSubTab::LinearAcceleration, 
                                        "Linear Acceleration").clicked() {
                                        self.bno_sub_tab = BnoSubTab::LinearAcceleration;
                                    }
                                    if let Some(( TelemetryPacket { hd_info: _, 
                                        packet: TelemetryEnum::BNO085CLASSIF(classif_pck)  }, _)) 
                                        = &classif_data {
                                        ui.label(format!("classification: {}", classif_pck.classif.to_str()));
                                    }
                                });
                                ui.separator();

                                match self.bno_sub_tab {
                                    BnoSubTab::LinearAcceleration => {
                                        if let Some(( TelemetryPacket { hd_info: _, 
                                        packet: TelemetryEnum::BNO085LINEARACCEL(lin_data)  }, _)) 
                                        = linear_accel_data {
                                            ui.label(format!("status: {}", lin_data.status.to_str()));
                                        }

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::Bno085LinearAccel,
                                            "BNO_LINEAR_ACCEL_X", "Acceleration X (g)",
                                            Some((-1.0, 1.0)),
                                            |pck| match pck {
                                                TelemetryEnum::BNO085LINEARACCEL(p) => Some(p.get_accel_g().0),
                                                _ => None,
                                            },
                                        );

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::Bno085LinearAccel,
                                            "BNO_LINEAR_ACCEL_Y", "Acceleration Y (g)",
                                            Some((-1.0, 1.0)),
                                            |pck| match pck {
                                                TelemetryEnum::BNO085LINEARACCEL(p) => Some(p.get_accel_g().1),
                                                _ => None,
                                            },
                                        );

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::Bno085LinearAccel,
                                            "BNO_LINEAR_ACCEL_Z", "Acceleration Z (g)",
                                            Some((-3.0, 3.0)),
                                            |pck| match pck {
                                                TelemetryEnum::BNO085LINEARACCEL(p) => Some(p.get_accel_g().2),
                                                _ => None,
                                            },
                                        );
                                    },
                                    BnoSubTab::Rotation => {
                                        if let Some(( TelemetryPacket { hd_info: _, 
                                        packet: TelemetryEnum::BNO085ROTATION(rot)  }, _))
                                        = rotation_data {
                                            ui.label(format!("status: {}", rot.status.to_str()));
                                            ui.label(format!("accuracy: {}°", rot.get_accuracy_deg()));
                                        }

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::Bno085Rotation,
                                            "BNO_ROT_ROLL", "Roll (°)",
                                            Some((-300.0, 300.0)),
                                            |pck| match pck {
                                                TelemetryEnum::BNO085ROTATION(p) => Some(p.get_angles_deg().0),
                                                _ => None,
                                            },
                                        );

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::Bno085Rotation,
                                            "BNO_ROT_PITCH", "Pitch (°)",
                                            Some((-300.0, 300.0)),
                                            |pck| match pck {
                                                TelemetryEnum::BNO085ROTATION(p) => Some(p.get_angles_deg().1),
                                                _ => None,
                                            },
                                        );

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::Bno085Rotation,
                                            "BNO_ROT_YAW", "Yaw (°)",
                                            Some((-300.0, 300.0)),
                                            |pck| match pck {
                                                TelemetryEnum::BNO085ROTATION(p) => Some(p.get_angles_deg().2),
                                                _ => None,
                                            },
                                        );
                                    },
                                    BnoSubTab::Acceleration => {
                                        if let Some(( TelemetryPacket { hd_info: _, 
                                        packet: TelemetryEnum::BNO085ACCEL(acc)  }, _))
                                        = accel_data {
                                            ui.label(format!("status: {}", acc.status.to_str()));
                                        }

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::Bno085Accel,
                                            "BNO_ACCEL_X", "Acceleration X (g)",
                                            Some((-1.0, 1.0)),
                                            |pck| match pck {
                                                TelemetryEnum::BNO085ACCEL(p) => Some(p.get_accel_g().0),
                                                _ => None,
                                            },
                                        );

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::Bno085Accel,
                                            "BNO_ACCEL_Y", "Acceleration Y (g)",
                                            Some((-1.0, 1.0)),
                                            |pck| match pck {
                                                TelemetryEnum::BNO085ACCEL(p) => Some(p.get_accel_g().1),
                                                _ => None,
                                            },
                                        );

                                        self.render_sensor_plot(
                                            ui, data, SensorKey::Bno085Accel,
                                            "BNO_ACCEL_Z", "Acceleration Z (g)",
                                            Some((-3.0, 3.0)),
                                            |pck| match pck {
                                                TelemetryEnum::BNO085ACCEL(p) => Some(p.get_accel_g().2),
                                                _ => None,
                                            },
                                        );
                                    }
                                }
                            },
                            Some(SensorTab::BMP280) => {
                                ui.heading("BMP280 - Pressure & temperature");

                                self.render_sensor_plot(
                                    ui, data, SensorKey::BMP,
                                    "BMP_TEMP", "BMP Temperature (°C)",
                                    Some((0.0, 50.0)),
                                    |pck| match pck {
                                        TelemetryEnum::BMP(p) => Some(p.get_temperature_deg()),
                                        _ => None,
                                    },
                                );

                                self.render_sensor_plot(
                                    ui, data, SensorKey::BMP,
                                    "BMP_PRESSURE", "Pressure (bar)",
                                    Some((0.8, 1.2)),
                                    |pck| match pck {
                                        TelemetryEnum::BMP(p) => Some(p.get_pressure_bar()),
                                        _ => None,
                                    },
                                );
                            },
                            Some(SensorTab::RFIDRC522) => {
                                ui.group(|ui| {
                                    ui.vertical(|ui| {
                                        ui.heading("RFID-RC522 - tag UID scan");
                                        ui.separator();

                                        let mut uid_counts: HashMap<String, usize> = HashMap::new();
                                        let mut last_scanned: Option<(String, f64)> = None;

                                        for (p, t) in data[SensorKey::Rcwl0515.index()].iter() {
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

                                // Event history build
                                for (p, t) in data[SensorKey::Rcwl0515.index()].iter() {
                                    if let TelemetryEnum::RCWL0515(motion) = &p.packet {
                                        currently_detected = motion.detection;
                                        history.push((*t, motion.detection));
                                    }
                                }

                                // Current state
                                ui.group(|ui| {
                                    ui.vertical_centered_justified(|ui| {
                                        if currently_detected {
                                            let text = egui::RichText::new(" MOVEMENT DETECTED ").size(24.0).strong().color(egui::Color32::WHITE);
                                            ui.colored_label(egui::Color32::from_rgb(200, 30, 30), text);
                                        } else {
                                            let text = egui::RichText::new("  RAS - No movement ").size(24.0).strong().color(egui::Color32::WHITE);
                                            ui.colored_label(egui::Color32::from_rgb(30, 150, 30), text);
                                        }
                                    });
                                });

                                ui.add_space(15.0);

                                // Event history panel
                                ui.collapsing("Record of events", |ui| {
                                    if history.is_empty() {
                                        ui.weak("No event stored");
                                    } else {
                                        // Most recent first
                                        history.reverse();

                                        egui::Grid::new("rcwl_history_table")
                                            .striped(true)
                                            .spacing([40.0, 6.0])
                                            .show(ui, |ui| {
                                                ui.label(egui::RichText::new("Time (s)").strong());
                                                ui.label(egui::RichText::new("Event").strong());
                                                ui.end_row();

                                                // Printing only 10 last events
                                                for (t, motion) in history.iter().take(10) {
                                                    ui.label(format!("{:.2}s", t));

                                                    if *motion {
                                                        ui.colored_label(egui::Color32::LIGHT_RED, "Movement start");
                                                    } else {
                                                        ui.colored_label(egui::Color32::LIGHT_GREEN, "Movement ending");
                                                    }
                                                    ui.end_row();
                                                }
                                            });
                                    }
                                });
                            },
                            Some(SensorTab::KY033) => {
                                ui.heading("KY-033 - Color tracking sensor");
                                self.render_sensor_plot(
                                    ui, data, SensorKey::Ky033,
                                    "KY033 Pulses", "Number of pulses",
                                    Some((0.0, 100.0)),
                                    |pck| match pck {
                                        TelemetryEnum::KY033(p) => Some(p.pulses as f64),
                                        _ => None,
                                    },
                                );
                            },
                            Some(SensorTab::ESP) => {
                                ui.heading("ESP32-S3 - System & Hardware Monitoring");

                                if let Some((TelemetryPacket { packet: TelemetryEnum::ESP(last_pck), .. }, _)) 
                                = &data[SensorKey::ESP.index()].back() {
                                    ui.group(|ui| {
                                        // --- HEADER SUMMARY ---
                                        ui.horizontal(|ui| {
                                            ui.label(format!("Last reset : {}", last_pck.reset_reason.as_str()));
                                            ui.separator();
                                            ui.label(format!("Angle : {}°", last_pck.angle));
                                            ui.separator();
                                            ui.label(format!("Motor : {}", last_pck.motor));
                                            ui.separator();
                                            let nb_sent = packet_sent.load(Ordering::Relaxed);
                                            ui.label(format!("Sent packets : {}", nb_sent));
                                            ui.separator();
                                            ui.label(format!("Received packets : {}", last_pck.nb_packets));
                                            ui.separator();
                                            let packets_diff = nb_sent.saturating_sub(last_pck.nb_packets);
                                            ui.label(format!("Packets lost : {}", packets_diff));
                                            ui.separator();
                                            ui.label(format!("Core0 : {}", last_pck.core0));
                                            ui.separator();
                                            ui.label(format!("Core1 : {}", last_pck.core1));
                                        });

                                        ui.add_space(5.0);

                                        // --- NETWORK ERRORS SUMMARY ---
                                        ui.horizontal(|ui| {
                                            ui.label(format!("UDP Drops: {}", last_pck.udp_drop));
                                            ui.separator();
                                            ui.label(format!("IP Drops: {}", last_pck.ip_drop));
                                            ui.separator();
                                            ui.label(format!("UDP Checksum Errs: {}", last_pck.udp_chkerr));
                                        });

                                        ui.add_space(5.0);

                                        // --- MEMORY BARS ---
                                        let dram_total = 320u32 * 1024u32;
                                        let dram_used: u32 = dram_total.saturating_sub(last_pck.free_dram);
                                        ui.horizontal(|ui| {
                                            ui.label("DRAM:");
                                            ui.add(
                                                egui::ProgressBar::new(dram_used as f32 / dram_total as f32)
                                                    .text(format!(
                                                        "{:.1} KB free (max block: {:.1} KB)", 
                                                        last_pck.free_dram as f32 / 1024.0,
                                                        last_pck.free_dram_block as f32 / 1024.0
                                                    ))
                                            );
                                        });

                                        let psram_total = 8u32 * 1024u32 * 1024u32;
                                        let psram_used = psram_total.saturating_sub(last_pck.free_psram);
                                        ui.horizontal(|ui| {
                                            ui.label("PSRAM:");
                                            ui.add(
                                                egui::ProgressBar::new(psram_used as f32 / psram_total as f32)
                                                    .text(format!(
                                                        "{:.2} MB free (max block: {:.2} MB)", 
                                                        last_pck.free_psram as f32 / (1024.0 * 1024.0),
                                                        last_pck.free_psram_block as f32 / (1024.0 * 1024.0)
                                                    ))
                                            );
                                        });
                                    });

                                    ui.add_space(10.0);

                                    // --- PLOTS ---

                                    // SOC Temperature
                                    self.render_sensor_plot(
                                        ui, data, SensorKey::ESP,
                                        "ESP_TEMPERATURE", "Temperature (°C)",
                                        Some((0.0, 60.0)),
                                        |pck| match pck {
                                            TelemetryEnum::ESP(p) => Some(p.esp_deg as f64),
                                            _ => None,
                                        },
                                    );

                                    // RSSI
                                    self.render_sensor_plot(
                                        ui, data, SensorKey::ESP,
                                        "ESP_WIFI_RSSI", "RSSI signal (dBm)",
                                        Some((-100.0, 10.0)),
                                        |pck| match pck {
                                            TelemetryEnum::ESP(p) => Some(p.rssi as f64),
                                            _ => None,
                                        },
                                    );

                                    // Core 0
                                    self.render_sensor_plot(
                                        ui, data, SensorKey::ESP,
                                        "ESP_CPU_CORE_0", "CPU Core 0 (%)",
                                        Some((0.0, 100.0)),
                                        |pck| match pck {
                                            TelemetryEnum::ESP(p) => Some(p.core0 as f64),
                                            _ => None,
                                        },
                                    );

                                    // Core 1
                                    self.render_sensor_plot(
                                        ui, data, SensorKey::ESP,
                                        "ESP_CPU_CORE_1", "CPU Core 1 (%)",
                                        Some((0.0, 100.0)),
                                        |pck| match pck {
                                            TelemetryEnum::ESP(p) => Some(p.core1 as f64),
                                            _ => None,
                                        },
                                    );
                                } else {
                                    ui.weak("No ESP data");
                                }
                            },
                            Some(SensorTab::BATADC) => {
                                ui.heading("Voltage of Battery by ESP's ADC");

                                self.render_sensor_plot(
                                    ui, data, SensorKey::BatAdc,
                                    "Battery voltage", "Voltage (V)",
                                    Some((0.0, 10.0)),
                                    |pck| match pck {
                                        TelemetryEnum::BATADC(p) => Some(p.adc_voltage as f64),
                                        _ => None,
                                    },
                                );
                            },
                            Some(SensorTab::ACS758) => {
                                ui.heading("Current going through ACS758 Current Hall Sensor");

                                self.render_sensor_plot(
                                    ui, data, SensorKey::ACS758,
                                    "Current of ACS758", "Current (A)",
                                    Some((-10.0, 40.0)),
                                    |pck| match pck {
                                        TelemetryEnum::ACS758(p) => Some(p.adc_current as f64),
                                        _ => None,
                                    },
                                );
                            },
                            Some(SensorTab::LIDAR) => {
                                ui.heading("Lidar D500 - Live Scan");

                                let diag = match shared_lidar_diag.try_read() {
                                    Ok(d) => d.clone(),
                                    Err(_) => LidarDiagnostics::default(),
                                };

                                let slam_state = match shared_slam.try_read() {
                                    Ok(s) => s.clone(),
                                    Err(_) => SlamState::default(),
                                };

                                ui.horizontal(|ui| {
                                    ui.label(format!("Lidar Speed: {} deg/s", diag.radar_speed_deg_s));
                                    ui.separator();
                                    ui.label(format!("Timestamp: {} ms", diag.timestamp_ms));
                                    ui.separator();
                                    ui.label(format!("Active Points: {}", diag.point_count));
                                    ui.separator();
                                    ui.label(format!("SLAM Pose: X: {:.2}, Y: {:.2}, Yaw: {:.2} rad", 
                                        slam_state.corrected_pose.0, 
                                        slam_state.corrected_pose.1, 
                                        slam_state.corrected_pose.2
                                    ));
                                });

                                // Non-blocking read from the shared state for the UI
                                let points = match shared_live_scan.try_read() {
                                    Ok(guard) => guard.clone(),
                                    Err(_) => Vec::new(),
                                };

                                ui.label(format!("Live points: {} | Accumulated Map points: {}", 
                                    points.len(), slam_state.map_points.len()));
                                ui.add_space(8.0);

                                Plot::new("lidar_live_plot")
                                    .data_aspect(1.0) // Keeps 1:1 scale so meters aren't distorted
                                    .width(ui.available_width())
                                    .height(ui.available_height()) // Fills the available space in the tab/panel
                                    .default_x_bounds(-5.0, 5.0)
                                    .default_y_bounds(-5.0, 5.0)
                                    .show(ui, |plot_ui| {


                                        let mut grid_points = Vec::new();
                                        for y in 0..slam_state.occupancy_grid.height {
                                            for x in 0..slam_state.occupancy_grid.width {
                                                let idx = y * slam_state.occupancy_grid.width + x;
                                                if slam_state.occupancy_grid.cells[idx] > 0.5 { // Seuil d'occupation
                                                    let wx = slam_state.occupancy_grid.origin.x + (x as f32 + 0.5) * slam_state.occupancy_grid.resolution;
                                                    let wy = slam_state.occupancy_grid.origin.y + (y as f32 + 0.5) * slam_state.occupancy_grid.resolution;
                                                    grid_points.push([wx as f64, wy as f64]);
                                                }
                                            }
                                        }
                                        plot_ui.points(Points::new("slam_map", grid_points)
                                            .radius(1.5).color(egui::Color32::LIGHT_BLUE));

                                        // 2. Current live scan (Sensor local frame)
                                        let live_plot_points: PlotPoints = points
                                            .iter()
                                            .map(|pt| [pt.x as f64, pt.y as f64])
                                            .collect();
                                        plot_ui.points(Points::new("live_scan", live_plot_points)
                                            .radius(2.0).color(egui::Color32::RED));

                                        // 3. Origin marker
                                        plot_ui.points(
                                            Points::new("origin", vec![[0.0, 0.0]])
                                                .radius(4.0)
                                                .color(egui::Color32::WHITE)
                                        );
                                    });
                            },
                            Some(SensorTab::BZ181) => {
                                ui.heading("BZ181 GNSS - Position, Fix & Satellites");

                                if let Some((TelemetryPacket { packet: TelemetryEnum::BZ181(last_pck), .. }, _)) 
                                = &data[SensorKey::BZ181.index()].back() {
                                    
                                    // --- SUMMARY GROUP 1 : STATUS & POSITION ---
                                    ui.group(|ui| {
                                        ui.horizontal(|ui| {
                                            let status_str = if last_pck.status == b'A' { "Active" } else { "Void" };
                                            ui.label(format!("Status : {} ({})", status_str, last_pck.status as char));
                                            ui.separator();
                                            ui.label(format!("Fix Quality : {}", last_pck.fix_quality));
                                            ui.separator();
                                            ui.label(format!("Fix Type : {}", last_pck.fix_type));
                                            ui.separator();
                                            ui.label(format!("Sats Used : {}", last_pck.satellites_used));
                                            ui.separator();
                                            ui.label(format!("Mode : {}", last_pck.fix_mode as char));
                                        });

                                        ui.add_space(5.0);

                                        ui.horizontal(|ui| {
                                            ui.label(format!("Latitude : {:.6}°", last_pck.latitude));
                                            ui.separator();
                                            ui.label(format!("Longitude : {:.6}°", last_pck.longitude));
                                            ui.separator();
                                            ui.label(format!("Altitude : {:.1} m", last_pck.altitude_m));
                                            ui.separator();
                                            ui.label(format!("Geoid : {:.1} m", last_pck.geoid_height_m));
                                        });

                                        ui.add_space(5.0);

                                        ui.horizontal(|ui| {
                                            ui.label(format!("Speed : {:.1} km/h", last_pck.speed_kmh));
                                            ui.separator();
                                            ui.label(format!("Course : {:.1}°", last_pck.course_over_ground));
                                            ui.separator();
                                            ui.label(format!("UTC Time : {}", last_pck.utc_time));
                                            ui.separator();
                                            ui.label(format!("UTC Date : {}", last_pck.utc_date));
                                        });
                                    });

                                    ui.add_space(5.0);

                                    // --- SUMMARY GROUP 2 : DOP & ACTIVE SATS ---
                                    ui.group(|ui| {
                                        ui.horizontal(|ui| {
                                            ui.label(format!("HDOP : {:.2}", last_pck.hdop));
                                            ui.separator();
                                            ui.label(format!("VDOP : {:.2}", last_pck.vdop));
                                            ui.separator();
                                            ui.label(format!("PDOP : {:.2}", last_pck.pdop));
                                            ui.separator();
                                            ui.label(format!("Total in View : {}", last_pck.total_sats_in_view));
                                        });

                                        ui.add_space(5.0);

                                        // Active Satellites IDs vector formatting
                                        let active_ids: Vec<String> = (0..last_pck.active_sats_count as usize)
                                            .filter(|&i| i < last_pck.active_sats_ids.len())
                                            .map(|i| last_pck.active_sats_ids[i].to_string())
                                            .collect();
                                        ui.label(format!("Active Satellites IDs (GSA): [{}]", active_ids.join(", ")));
                                    });

                                    ui.add_space(10.0);

                                    // --- SATELLITES IN VIEW TABLE (GSV) ---
                                    ui.heading("Satellites in View (GSV)");
                                    egui::ScrollArea::vertical().max_height(220.0).show(ui, |ui| {
                                        egui::Grid::new("bz181_sats_grid").striped(true).show(ui, |ui| {
                                            ui.strong("PRN");
                                            ui.strong("Constellation");
                                            ui.strong("Elevation");
                                            ui.strong("Azimuth");
                                            ui.strong("SNR");
                                            ui.end_row();

                                            for sat in &last_pck.sats_in_view {
                                                if sat.prn == 0 { continue; } // Ignore empty slots
                                                
                                                ui.label(format!("{:02}", sat.prn));
                                                
                                                let const_str = sat.constellation.to_str();
                                                ui.label(const_str);
                                                ui.label(format!("{}°", sat.elevation));
                                                ui.label(format!("{}°", sat.azimuth));
                                                ui.label(format!("{} dBHz", sat.snr));
                                                ui.end_row();
                                            }
                                        });
                                    });

                                    ui.add_space(10.0);

                                    // --- PLOTS ---

                                    // HDOP Plot
                                    self.render_sensor_plot(
                                        ui, data, SensorKey::BZ181,
                                        "BZ181_HDOP", "HDOP (Precision)",
                                        Some((0.0, 5.0)),
                                        |pck| match pck {
                                            TelemetryEnum::BZ181(p) => Some(p.hdop as f64),
                                            _ => None,
                                        },
                                    );

                                    // Satellites Used Plot
                                    self.render_sensor_plot(
                                        ui, data, SensorKey::BZ181,
                                        "BZ181_SATS_USED", "Satellites Used Count",
                                        Some((0.0, 30.0)),
                                        |pck| match pck {
                                            TelemetryEnum::BZ181(p) => Some(p.satellites_used as f64),
                                            _ => None,
                                        },
                                    );

                                } else {
                                    ui.weak("No BZ181 GPS data");
                                }
                            }
                            None => {
                                ui.centered_and_justified(|ui| {
                                    ui.weak("Select a sensor...");
                                });
                            },
                        }
                    } else {
                        ui.weak(format!("No valid sensor data for ID #{}", active_id));
                    }
                } else {
                    ui.weak(format!("No valid ESP ID"));
                }
            });
        });

    }
}