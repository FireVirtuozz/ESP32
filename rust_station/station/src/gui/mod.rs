use std::{collections::{HashMap, VecDeque}, sync::{Arc, RwLock, atomic::{AtomicBool, AtomicU32, AtomicU64}, mpsc::{self, Sender}}, time::{Duration, Instant}};

use arc_swap::ArcSwap;
use egui::Vec2b;
use egui_plot::{Line, Plot, PlotBounds, PlotPoints};
use log::debug;
use nalgebra::Point2;

use crate::{camera::CameraConfig, config::AppConfig, control::{DriveCommand, MiscellaneousCommand, SafetyCommand}, controller::ControllerPacket, gui::screens::{camera::CameraScreen, car::CarScreen, commands::CommandsScreen, dashboard::DashboardScreen, dump::{DumpEntry, DumpScreen}, home::HomeScreen, logs::{LogPacket, LogsScreen}, main::MainScreen, ota_control::OtaScreen, sensors::SensorsScreen, tuning::TuningScreen}, odometry::{ProcessedKey, ProcessedTelemetry, odo::OdoState, slam::SlamState}, sensors::{SensorKey, TelemetryPacket, lidar::LidarDiagnostics}, udp::udp_cfg::{CamCmd, CamEvent, SharedCamCfg}};

pub mod screens;

use std::sync::OnceLock;

#[derive(PartialEq)]
pub enum ScreensTypes {
    Home,
    Main,
    Sensors,
    Logs,
    Commands,
    Camera,
    Dump,
    Car,
    Tuning,
    Ota,
    Dashboard,
}

#[derive(Default)]
pub struct Screens {
    pub sensors_screen: SensorsScreen,
    pub commands_screen: CommandsScreen,
    pub home_screen: HomeScreen,
    pub logs_screen: LogsScreen,
    pub main_screen: MainScreen,
    pub camera_screen: CameraScreen,
    pub dump_screen: DumpScreen,
    pub car_screen: CarScreen,
    pub tuning_screen: TuningScreen,
    pub ota_screen: OtaScreen,
    pub dashboard_screen: DashboardScreen,
    pub cam_config: CameraConfig,
}

pub struct MyApp {

    pub devices: HashMap<u8, [VecDeque<(TelemetryPacket, f64)>; SensorKey::COUNT]>,
    pub data_processed: [VecDeque<(ProcessedTelemetry, f64)>; ProcessedKey::COUNT],
    pub logs: VecDeque<LogPacket>,
    pub start: Instant,
    pub screen: ScreensTypes,
    pub dumps: Vec<DumpEntry>,
    pub cam_config: CameraConfig,

    pub tx_cam: Sender<CameraConfig>,
    pub tx_cmd: Sender<CamCmd>,

    pub screens: Screens,

    pub logs_connected: Arc<AtomicBool>,
    pub sensors_connected: Arc<AtomicBool>,
    pub controller_connected: Arc<AtomicBool>,
    pub camera_connected: Arc<AtomicBool>,

    pub rx_sensors: crossbeam_channel::Receiver<TelemetryPacket>,

    pub rx_ctrl: mpsc::Receiver<ControllerPacket>,
    pub rx_logs: mpsc::Receiver<LogPacket>,
    pub rx_dump: mpsc::Receiver<DumpEntry>,
    pub rx_processed: mpsc::Receiver<ProcessedTelemetry>,
    pub rx_ev: mpsc::Receiver<CamEvent>,

    pub config_egui: AppConfig,

    pub shared_texture: Arc<RwLock<Option<egui::ColorImage>>>,
    pub shared_fps: Arc<AtomicU32>,
    pub shared_frame_count: Arc<AtomicU64>,
    pub shared_odo: Arc<RwLock<OdoState>>,
    pub shared_slam: Arc<RwLock<SlamState>>,
    pub packet_sent: Arc<AtomicU32>,
    pub shared_live_scan: Arc<RwLock<Vec<Point2<f32>>>>,
    pub shared_lidar_diag: Arc<RwLock<LidarDiagnostics>>,
    pub shared_waypoints: Arc<RwLock<Vec<[f64; 2]>>>,
    pub shared_predicted_path: Arc<RwLock<Vec<[f64; 2]>>>,
    pub shared_cfg: SharedCamCfg,

    pub active_drive: Arc<ArcSwap<DriveCommand>>,
    pub active_safety: Arc<ArcSwap<SafetyCommand>>,
    pub active_misc: Arc<ArcSwap<MiscellaneousCommand>>,

    pub last_frame_time: Instant,
    pub fps_display: f32,
}

impl eframe::App for MyApp {

    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        const TARGET_FPS: f32 = 60.0;
        let target_duration = Duration::from_secs_f32(1.0 / TARGET_FPS);

        let elapsed = self.last_frame_time.elapsed();
        if elapsed < target_duration {
            std::thread::sleep(target_duration - elapsed);
        }

        let now = std::time::Instant::now();
        let dt = now.duration_since(self.last_frame_time).as_secs_f32();
        self.last_frame_time = now;
        if dt > 0.0 {
            let instant_fps = 1.0 / dt;
            self.fps_display = self.fps_display * 0.9 + instant_fps * 0.1;
        }

        let ctx = ui.ctx().clone();

        egui::Area::new("fps_overlay".into())
        .anchor(egui::Align2::RIGHT_TOP, egui::vec2(-10.0, 10.0))
        .show(&ctx, |ui| {
            ui.set_min_width(80.0);
            ui.colored_label(
                if self.fps_display < 30.0 { egui::Color32::RED } else { egui::Color32::GREEN },
                format!("{:.0} FPS", self.fps_display)
            );
        });

        while let Ok(packet) = self.rx_sensors.try_recv() {
            let t = self.start.elapsed().as_secs_f64();
            let key = SensorKey::from(&packet.packet);

            let device_buffers = self.devices
            .entry(packet.hd_info.esp_id).or_insert_with(|| {
                // Crée un tableau de VecDeque vides pour chaque capteur (taille = NB_SENSOR_KEYS)
                std::array::from_fn(|_| VecDeque::new())
            });

            let buffer = &mut device_buffers[key.index()];
            buffer.push_back((packet, t));
            
            if buffer.len() > 1000 {
                buffer.pop_front();
            }
        }

        while let Ok(packet) = self.rx_processed.try_recv() {
            let t = self.start.elapsed().as_secs_f64();
            let key = ProcessedKey::from(&packet);

            let buffer = &mut self.data_processed[key.index()];
            buffer.push_back((packet, t));
            
            if buffer.len() > 1000 {
                buffer.pop_front();
            }
        }

        while let Ok(packet) = self.rx_logs.try_recv() {
            self.logs.push_back(packet);
            if self.logs.len() > 1000 {
                self.logs.pop_front();
            }
        }

        while let Ok(packet) = self.rx_dump.try_recv() {
            self.dumps.push(packet);
        }

        match self.screen {
            ScreensTypes::Sensors => self.screens.sensors_screen.show(&ctx, &self.devices, &mut self.screen, 
                &self.data_processed, &self.packet_sent, &self.shared_live_scan, &self.shared_lidar_diag,
                &self.shared_slam),
            ScreensTypes::Commands => self.screens.commands_screen.show(
                &ctx, &self.controller_connected, &self.rx_ctrl, &mut self.screen),
            ScreensTypes::Home => self.screens.home_screen.show(&ctx, &mut self.screen),
            ScreensTypes::Logs => self.screens.logs_screen.show(&ctx, &self.logs, &mut self.screen),
            ScreensTypes::Main => self.screens.main_screen.show(&ctx,
                &mut self.screen, &self.sensors_connected, &self.logs_connected, &self.camera_connected),
            ScreensTypes::Camera => self.screens.camera_screen.show(&ctx, &mut self.screen,
                &self.shared_texture, &self.shared_fps, &self.shared_frame_count, 
                &self.shared_cfg, &self.tx_cmd, &self.rx_ev),
            ScreensTypes::Dump => self.screens.dump_screen.show(&ctx, &self.dumps, &mut self.screen),
            ScreensTypes::Car => self.screens.car_screen.show(&ctx, &mut self.screen, &self.devices,
                &self.controller_connected, &self.rx_ctrl, &self.start,
                &self.shared_texture, &self.shared_fps, &self.shared_frame_count, 
                &self.shared_odo, &self.shared_slam, &self.packet_sent, &self.shared_live_scan,
                &self.active_drive, &self.active_safety, &self.active_misc,
                &self.shared_waypoints, &self.shared_predicted_path,
            ),
            ScreensTypes::Tuning => self.screens.tuning_screen.show(&ctx, &self.devices, &self.config_egui, &mut self.screen),
            ScreensTypes::Ota => self.screens.ota_screen.show(&ctx, &mut self.screen, &self.config_egui),
            ScreensTypes::Dashboard => self.screens.dashboard_screen.show(&ctx, &self.devices, &mut self.screen),
        }

        //ctx.request_repaint();
        ctx.request_repaint_after(target_duration);
    }
}

pub fn get_sensor_data<'a>(
    devices: &'a HashMap<u8, [VecDeque<(TelemetryPacket, f64)>; SensorKey::COUNT]>,
    selected_id: &Option<u8>,
    sensor_key: SensorKey,
) -> Option<&'a VecDeque<(TelemetryPacket, f64)>> {
    if let Some(active_id) = selected_id {
        if let Some(data) = devices.get(&active_id) {
            return Some(&data[sensor_key.index()]);
        }
    }
    None
}