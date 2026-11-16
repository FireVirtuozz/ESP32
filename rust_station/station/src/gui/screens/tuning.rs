use std::{collections::{HashMap, VecDeque}, net::UdpSocket, sync::mpsc::Sender};
use egui_plot::{Line, Plot, PlotPoints};
use log::error;
use serde::{Deserialize, Serialize};
use crate::{config::AppConfig, gui::{ScreensTypes, get_sensor_data}, sensors::{SensorKey, TelemetryEnum, TelemetryPacket}, udp::ConfigCommandType};

#[derive(PartialEq, Clone, Copy, Serialize, Deserialize, Debug)]
pub enum CurveType { Linear = 0, Exp = 1, Cosine = 2 }

impl CurveType {
    pub fn from_u8(val: u8) -> Self {
        match val {
            0 => CurveType::Linear,
            1 => CurveType::Exp,
            2 => CurveType::Cosine,
            _ => CurveType::Linear,
        }
    }

    pub fn to_u8(&self) -> u8 {
        match self {
            CurveType::Linear => 0,
            CurveType::Exp => 1,
            CurveType::Cosine => 2,
        }
    }
}

pub struct TuningParams {
    pub curve_type: CurveType,
    pub accel_param: u8,
    pub decel_param: u8,
}

impl Default for TuningParams {
    fn default() -> Self {
        Self {
            accel_param: 5,
            decel_param: 150,
            curve_type: CurveType::Linear,
        }
    }
}

impl TuningParams {
    pub fn to_bytes(&self) -> [u8; 4] {
        let mut frame = [0u8; 4];
        frame[0] = ConfigCommandType::Tuning.to_u8();
        frame[1] = self.curve_type.to_u8();
        frame[2] = self.accel_param;
        frame[3] = self.decel_param;
        frame
    }
}

pub struct TuningScreen {
    pub tuning_params: TuningParams,
    pub socket_udp_config: UdpSocket,
    pub selected_id: Option<u8>,
}

impl Default for TuningScreen {
    fn default() -> Self {
        Self {
            tuning_params: TuningParams::default(),
            socket_udp_config: UdpSocket::bind("0.0.0.0:0").unwrap(),
            selected_id: None,
        }
    }
}

impl TuningScreen {
    fn simulate_ramp(curve: CurveType, param: u8, target: i32, steps: usize) -> Vec<[f64; 2]> {
        let mut current: i32 = 0;
        let mut ramp_tick: u32 = 0;
        let ramp_start_motor: i32 = 0;

        (0..steps).map(|i| {
            let delta = target - current;
            if delta != 0 {
                current = match curve {
                    CurveType::Linear => {
                        let step = if delta > 0 { param as i32 } else { -(param as i32) };
                        let next = current + step;
                        if delta > 0 { next.min(target) } else { next.max(target) }
                    }
                    CurveType::Exp => {
                        let alpha = param as f32 / 255.0;
                        current + (delta as f32 * alpha) as i32
                    }
                    CurveType::Cosine => {
                        ramp_tick += 1;
                        let total_ticks = (200u32.saturating_sub(param as u32)).max(1);
                        let t = (ramp_tick as f32 / total_ticks as f32).min(1.0);
                        let s = (1.0 - (std::f32::consts::PI * t).cos()) / 2.0;
                        ramp_start_motor + ((target - ramp_start_motor) as f32 * s) as i32
                    }
                };
            }
            [i as f64 * 0.02, current as f64] // dt = 20ms, like MOTOR_CTRL_PERIOD in ESP
        }).collect()
    }

    pub fn show(
        &mut self,
        ctx: &egui::Context,
        devices: &HashMap<u8, [VecDeque<(TelemetryPacket, f64)>; SensorKey::COUNT]>,
        config_egui: &AppConfig,
        screen: &mut ScreensTypes,
    ) {

        egui::SidePanel::left("esp_sidebar").min_width(150.0).show(ctx, |ui| {
            ui.heading("Connected ESPs");
            ui.separator();

            let mut ids: Vec<u8> = devices.keys().cloned().collect();
            ids.sort(); // Pour garder un ordre stable dans la liste

            for id in ids {
                let is_selected = self.selected_id == Some(id);
                if ui.selectable_label(is_selected, format!("ESP #{0}", id)).clicked() {
                    self.selected_id = Some(id);
                }
            }

            if devices.is_empty() {
                ui.add_space(10.0);
                ui.weak("Waiting for ESPs...");
            }
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            if ui.button("Back").clicked() { *screen = ScreensTypes::Main; }
            ui.heading("Drive Profile Tuning");

            ui.horizontal(|ui| {
                ui.selectable_value(&mut self.tuning_params.curve_type, CurveType::Linear, "Linear");
                ui.selectable_value(&mut self.tuning_params.curve_type, CurveType::Exp, "Exp");
                ui.selectable_value(&mut self.tuning_params.curve_type, CurveType::Cosine, "Cosine");
            });

            ui.add(egui::Slider::new(&mut self.tuning_params.accel_param, 0..=255).text("accel_param"));
            ui.add(egui::Slider::new(&mut self.tuning_params.decel_param, 0..=255).text("decel_param"));

            ui.horizontal(|ui| {
                ui.vertical(|ui| {
                    ui.label("Simulated acceleration");
                    let sim_accel = Self::simulate_ramp(
                        self.tuning_params.curve_type, self.tuning_params.accel_param, 1000, 200);
                    Plot::new("sim_preview_accel")
                        .height(300.0)
                        .width(400.0)
                        .show(ui, |plot_ui| {
                            plot_ui.line(Line::new("simulated", PlotPoints::from(sim_accel)).color(egui::Color32::GRAY));
                        });

                    ui.label("Simulated deceleration");
                    let sim_decel = Self::simulate_ramp(
                        self.tuning_params.curve_type, self.tuning_params.decel_param, -1000, 200);
                    Plot::new("sim_preview_decel")
                        .height(300.0)
                        .width(400.0)
                        .show(ui, |plot_ui| {
                            plot_ui.line(Line::new("simulated", PlotPoints::from(sim_decel)).color(egui::Color32::GRAY));
                        });
                });

                ui.vertical(|ui| {
                    ui.label("Real telemetry (5s window)");

                    if let Some (motor_buffer) 
                    = get_sensor_data(devices, &self.selected_id, SensorKey::Motor) {

                        let now_ts = motor_buffer.back().map(|(_, t)| *t).unwrap_or(0.0);
                        let window_start = now_ts - 5.0;

                        let (real_current, real_target): (Vec<[f64; 2]>, Vec<[f64; 2]>) 
                        = motor_buffer
                            .iter()
                            .filter_map(|(p, t)| {
                                if *t >= window_start {
                                    if let TelemetryEnum::MOTOR(m) = &p.packet {
                                        let rel_t = *t - now_ts;
                                        return Some(([rel_t, m.current_motor as f64], [rel_t, m.target_motor as f64]));
                                    }
                                }
                                None
                            })
                            .unzip();

                        Plot::new("real_ramp")
                            .height(600.0)
                            .width(900.0)
                            .include_x(-5.0) 
                            .include_x(0.0)  
                            .show(ui, |plot_ui| {
                                plot_ui.line(
                                    Line::new("current", PlotPoints::from(real_current))
                                        .color(egui::Color32::from_rgb(220, 60, 60))
                                );
                                plot_ui.line(
                                    Line::new("target", PlotPoints::from(real_target))
                                        .color(egui::Color32::from_rgb(80, 180, 255))
                                        .style(egui_plot::LineStyle::Dashed { length: 8.0 })
                                );
                            });
                    } else {
                        ui.weak("No valid ESP ID");
                    }
                });
            });

            if ui.button("Send motor config to ESP").clicked() {
                let addr = format!("{}:{}", config_egui.ip_addr_esp0, config_egui.udp_port_config);
                if let Err(e) = self.socket_udp_config.send_to(&self.tuning_params.to_bytes(), addr) {
                    error!("Error sending tuning config: {e}");
                }
            }
        });
    }
}