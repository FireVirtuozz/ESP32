use std::collections::VecDeque;

use egui::Vec2b;
use egui_plot::{Line, Plot, PlotBounds, PlotPoints};

use crate::{gui::MyApp, monitor::TelemetryPacket};

pub struct SensorsScreen {
    pub show_ultrasonic: bool,
    pub show_ina: bool,
    pub show_mpu: bool,
    pub show_bmp: bool,
    pub show_hall: bool,
}

impl Default for SensorsScreen {
    fn default() -> Self {
        Self {
            show_ultrasonic: true,
            show_ina: true,
            show_mpu: true,
            show_bmp: true,
            show_hall: true,
        }
    }
}

impl SensorsScreen {
    pub fn show(&mut self, ctx: &egui::Context, data: &VecDeque<(TelemetryPacket, f64)>) {

        egui::TopBottomPanel::top("sensors_filter").show(ctx, |ui| {
            ui.horizontal(|ui| {
                let all = self.show_ultrasonic && self.show_ina && self.show_mpu && self.show_bmp && self.show_hall;
                if ui.selectable_label(all, "All").clicked() {
                    let new_val = !all;
                    self.show_ultrasonic = new_val;
                    self.show_ina = new_val;
                    self.show_mpu = new_val;
                    self.show_bmp = new_val;
                    self.show_hall = new_val;
                }
                ui.separator();
                ui.toggle_value(&mut self.show_ultrasonic, "HC-SR04");
                ui.toggle_value(&mut self.show_ina, "INA226");
                ui.toggle_value(&mut self.show_mpu, "MPU");
                ui.toggle_value(&mut self.show_bmp, "BMP280");
                ui.toggle_value(&mut self.show_hall, "KY-003");
            });
        });

        let latest_t = data.back().map_or(0.0, |(_, t)| *t);
        const TIME_WINDOW: f64 = 30.0;

        egui::CentralPanel::default().show(ctx, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {

                let mut make_plot = |name: &str,
                                    y_label: &str,
                                    y_range: Option<(f64, f64)>,
                                    points_fn: fn(&VecDeque<(TelemetryPacket, f64)>) -> PlotPoints| {
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
                            let points = points_fn(data);
                            plot_ui.line(Line::new(points).width(2.5));

                            let min_t = (latest_t - TIME_WINDOW).max(0.0);

                            let (y_min, y_max) = y_range.unwrap_or((0.0, 0.0));
                            plot_ui.set_plot_bounds(PlotBounds::from_min_max(
                                [min_t, y_min],
                                [latest_t, y_max],
                            ));

                            let auto_y = y_range.is_none();
                            plot_ui.set_auto_bounds(Vec2b::new(false, auto_y));
                        });
                };

                if self.show_ultrasonic {
                    make_plot("HC-SR04_DISTANCE", "Distance (cm)", Some((0.0, 2000.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| p.ultrasonic.as_ref().map(|u| [*t, u.get_distance_cm()]))
                            .collect()
                    });
                }

                if self.show_ina {
                    make_plot("INA_BUS_VOLTAGE", "Bus voltage (V)", Some((-6.0, 6.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| p.ina.as_ref().map(|u| [*t, u.get_bus_voltage_v()]))
                            .collect()
                    });

                    make_plot("INA_SHUNT_VOLTAGE", "Shunt voltage (mV)", Some((-2.0, 2.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| p.ina.as_ref().map(|u| [*t, u.get_shunt_voltage_mv()]))
                            .collect()
                    });

                    make_plot("INA_CURRENT", "Current (mA)", Some((0.0, 30.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| p.ina.as_ref().map(|u| [*t, u.get_current_ma()]))
                            .collect()
                    });

                    make_plot("INA_POWER", "Power (mW)", Some((0.0, 50.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| p.ina.as_ref().map(|u| [*t, u.get_power_mw()]))
                            .collect()
                    });
                }

                if self.show_hall {
                    make_plot("KY_SPEED", "Speed (km/h)", Some((0.0, 100.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| {
                                p.hall.as_ref().and_then(|h| h.get_speed_km_h().ok().map(|v| [*t, v]))
                            })
                            .collect()
                    });

                    make_plot("KY_DISTANCE", "Cumulated distance (m)", None, |data| {
                        data.iter()
                            .filter_map(|(p, t)| {
                                p.hall.as_ref().map(|h| [*t, h.get_distance_m()])
                            })
                            .collect()
                    });
                }

                if self.show_mpu {
                    make_plot("MPU_ACCEL_X", "Acceleration X (g)", Some((-3.0, 3.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| {
                                p.imu.as_ref().map(|imu| {
                                    let (ax, _, _) = imu.get_accel_g();
                                    [*t, ax]
                                })
                            })
                            .collect()
                    });

                    make_plot("MPU_ACCEL_Y", "Acceleration Y (g)", Some((-3.0, 3.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| {
                                p.imu.as_ref().map(|imu| {
                                    let (_, ay, _) = imu.get_accel_g();
                                    [*t, ay]
                                })
                            })
                            .collect()
                    });

                    make_plot("MPU_ACCEL_Z", "Acceleration Z (g)", Some((-3.0, 3.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| {
                                p.imu.as_ref().map(|imu| {
                                    let (_, _, az) = imu.get_accel_g();
                                    [*t, az]
                                })
                            })
                            .collect()
                    });

                    make_plot("MPU_GYRO_X", "Gyro X (°/s)", Some((-300.0, 300.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| {
                                p.imu.as_ref().map(|imu| {
                                    let (gx, _, _) = imu.get_gyro_deg_s();
                                    [*t, gx]
                                })
                            })
                            .collect()
                    });

                    make_plot("MPU_GYRO_Y", "Gyro Y (°/s)", Some((-300.0, 300.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| {
                                p.imu.as_ref().map(|imu| {
                                    let (_, gy, _) = imu.get_gyro_deg_s();
                                    [*t, gy]
                                })
                            })
                            .collect()
                    });

                    make_plot("MPU_GYRO_Z", "Gyro Z (°/s)", Some((-300.0, 300.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| {
                                p.imu.as_ref().map(|imu| {
                                    let (_, _, gz) = imu.get_gyro_deg_s();
                                    [*t, gz]
                                })
                            })
                            .collect()
                    });

                    make_plot("MPU_TEMP", "MPU Temperature (°C)", Some((0.0, 50.0)), |data| {
                    data.iter()
                        .filter_map(|(p, t)| {
                            p.imu.as_ref().map(|imu| [*t, imu.get_temperature_chip_deg()])
                        })
                        .collect()
                    });
                }

                if self.show_bmp {
                    make_plot("BMP_TEMP", "BMP Temperature (°C)", Some((0.0, 50.0)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| {
                                p.imu.as_ref().map(|imu| [*t, imu.get_temperature_deg()])
                            })
                            .collect()
                    });

                    make_plot("BMP_PRESSURE", "Pressure (bar)", Some((0.8, 1.2)), |data| {
                        data.iter()
                            .filter_map(|(p, t)| {
                                p.imu.as_ref().map(|imu| [*t, imu.get_pressure_bar()])
                            })
                            .collect()
                    });
                }
            });
        });
    }
}