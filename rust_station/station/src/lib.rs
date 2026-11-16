use std::{array, collections::{HashMap, VecDeque}, sync::{Arc, RwLock, atomic::{AtomicBool, AtomicU8, AtomicU32, AtomicU64}, mpsc}, thread, time::Instant};

pub mod config;
pub mod controller;
pub mod error;
pub mod gui;
pub mod sensors;
pub mod udp;
pub mod recorder;
pub mod ota;
pub mod ai;
pub mod control;
pub mod camera;
pub mod odometry;

use arc_swap::ArcSwap;
use config::AppConfig;
use error::AppError;
use gui::{MyApp, Screens, ScreensTypes};
use nalgebra::Point2;
use udp::{
    udp_dump::udp_server_dump_init, udp_logs::udp_logs_server_init,
    udp_sensors::udp_sensors_server_init, udp_video::udp_server_video_init,
};

pub fn build_app(config: AppConfig) -> MyApp {
    
    let (tx_logs, rx_logs) = mpsc::channel();
    let (tx_ctrl, rx_ctrl) = mpsc::channel();
    let (tx_img, rx_img) = mpsc::channel();
    let (tx_dump, rx_dump) = mpsc::channel();
    let (tx_record, rx_record) = mpsc::channel();
    let (tx_cam, rx_cam) = mpsc::channel();
    let (tx_processed, rx_processed) = mpsc::channel();
    let (tx_cmd, rx_cmd) = mpsc::channel();
    let (tx_ev, rx_ev) = mpsc::channel();
    
    let (tx_slam, rx_slam) = crossbeam_channel::bounded(1000);
    
    let (tx_sensors, rx_sensors) = crossbeam_channel::bounded(1000);
    let (tx_control, rx_control) = crossbeam_channel::bounded(1000);
    let (tx_misc, rx_misc) = crossbeam_channel::bounded(1000);
    let (tx_safety, rx_safety) = crossbeam_channel::unbounded();

    let (tx_integ, rx_integ) = crossbeam_channel::bounded(1000);
    let (tx_odo, rx_odo) = crossbeam_channel::bounded(1000);
    let (tx_lidar, rx_lidar) = crossbeam_channel::bounded(1000);

    let sensors_connected = Arc::new(AtomicBool::new(false));
    let logs_connected = Arc::new(AtomicBool::new(false));
    let controller_connected = Arc::new(AtomicBool::new(false));
    let camera_connected = Arc::new(AtomicBool::new(false));

    let active_mode = Arc::new(ModeState::new());
    let active_source = Arc::new(ActiveSource::default());

    let shared_texture : Arc<RwLock<Option<egui::ColorImage>>> = Arc::new(RwLock::new(None));
    let shared_fps: Arc<AtomicU32> = Arc::new(AtomicU32::new(0));
    let shared_frame_count : Arc<AtomicU64> = Arc::new(AtomicU64::new(0));
    let shared_odo = Arc::new(RwLock::new(OdoState::default()));
    let shared_slam : Arc<RwLock<SlamState>> = Arc::new(RwLock::new(SlamState::default()));
    let shared_waypoints : Arc<RwLock<Vec<[f64; 2]>>> = Arc::new(RwLock::new(Vec::new()));
    let shared_predicted_path : Arc<RwLock<Vec<[f64; 2]>>> = Arc::new(RwLock::new(Vec::new()));
    let packet_sent : Arc<AtomicU32> = Arc::new(AtomicU32::new(0));
    let shared_live_scan: Arc<RwLock<Vec<Point2<f32>>>> = Arc::new(RwLock::new(Vec::with_capacity(500)));
    let shared_lidar_diag: Arc<RwLock<LidarDiagnostics>> = Arc::new(RwLock::new(LidarDiagnostics::default()));
    let shared_cam_cfg: SharedCamCfg = Arc::new(RwLock::new(None));

    let active_drive: Arc<ArcSwap<DriveCommand>> = Arc::new(ArcSwap::from_pointee(DriveCommand::default()));
    let active_safety: Arc<ArcSwap<SafetyCommand>> = Arc::new(ArcSwap::from_pointee(SafetyCommand::default()));
    let active_misc: Arc<ArcSwap<MiscellaneousCommand>> = Arc::new(ArcSwap::from_pointee(MiscellaneousCommand::default()));

    let start_instant = Instant::now();

    // No controller for Android
    #[cfg(not(target_os = "android"))]
    {
        let _handle_ctrl = controller::init_controller(
            tx_ctrl, 
            tx_control,
            tx_safety,
            tx_misc,
            Arc::clone(&controller_connected),
            Arc::clone(&active_mode),
        );
    }
    #[cfg(target_os = "android")]
    {
        let _ = tx_ctrl; // sender dropped as no controller
    }

    if config.replay {
        let _handle_replay_sensor = replay_recording(
            tx_sensors, 
            config.clone(), 
            Arc::clone(&sensors_connected),
            tx_integ,
            tx_odo,
            tx_lidar,
        );
    } else {
        let _handle_udp_sensors = udp_sensors_server_init(
            tx_sensors,
            Arc::clone(&sensors_connected),
            config.clone(),
            start_instant,
            tx_record,
            tx_integ,
            tx_odo,
            tx_lidar,
        );
    }

    let _handle_control_thread = control_thread_spawn(
        rx_control, 
        rx_safety, 
        rx_misc,
        config.clone(), 
        start_instant, 
        active_source,
        Arc::clone(&packet_sent),
        Arc::clone(&active_drive),
        Arc::clone(&active_safety),
        Arc::clone(&active_misc),
        Arc::clone(&shared_waypoints),
        Arc::clone(&shared_slam),
        Arc::clone(&shared_predicted_path),
    );

    let _handle_udp_logs = udp_logs_server_init(
        tx_logs,
        Arc::clone(&logs_connected),
        config.clone(),
    );

    let _handle_udp_dump = udp_server_dump_init(tx_dump, config.clone());

    let _handle_udp_vid = udp_server_video_init(
        tx_img,
        Arc::clone(&camera_connected),
        config.clone(),
    );

    let _handle_udp_cfg = udp_cfg_server_init(
        Arc::clone(&shared_cam_cfg),
        config.clone(),
        rx_cmd,
        tx_ev,
    );

    let _handle_cam_decode = camera_decode_thread(
        rx_img, 
        Arc::clone(&shared_texture),
        Arc::clone(&shared_fps),
        Arc::clone(&shared_frame_count),
        Arc::clone(&shared_cam_cfg),
    );

    let _handle_integ = spawn_integration_thread(
        rx_integ,
        tx_processed,
        start_instant,
    );

    let _handle_odo = spawn_odo_thread(
        rx_odo,
        Arc::clone(&shared_odo)
    );

    let _handle_lidar = spawn_lidar_accumulator_thread(
        rx_lidar, 
        tx_slam,
        Arc::clone(&shared_odo),
        Arc::clone(&shared_live_scan),
        Arc::clone(&shared_lidar_diag),
    );

    /*
    let _handle_slam = spawn_slam_thread(
        rx_slam, 
        Arc::clone(&shared_slam),
    );
    */
    let _handle_slam = spawn_compare_thread(
        rx_slam, 
        Arc::clone(&shared_slam),
        //MatchAlgo::ScanToScan
        //MatchAlgo::SubmapIcp { keep_scans: 6, voxel_m: 0.05 }
        MatchAlgo::SubmapNdt { keep_scans: 8, voxel_m: 0.05, ndt_voxel_m: 0.5 }
    );

    if config.recording {
        let _handle_record = recorder_init(
            rx_record,
            config.clone(),
        );
    }
    
    MyApp {
        devices: HashMap::new(),
        data_processed: array::from_fn(|_| {VecDeque::new()}),
        logs: VecDeque::new(),
        start: Instant::now(),
        screen: ScreensTypes::Home,
        dumps: Vec::new(),

        cam_config: CameraConfig::default(),
        tx_cam,
        tx_cmd,

        screens: Screens::default(),

        logs_connected,
        sensors_connected,
        controller_connected,
        camera_connected,

        rx_sensors,
        rx_ctrl,
        rx_logs,
        rx_dump,
        rx_processed,
        rx_ev,

        config_egui: config,
        shared_fps,
        shared_texture,
        shared_frame_count,
        shared_odo,
        shared_slam,
        packet_sent,
        shared_live_scan,
        shared_lidar_diag,
        shared_waypoints,
        shared_predicted_path,
        shared_cfg: shared_cam_cfg,

        active_drive,
        active_safety,
        active_misc,

        fps_display: 30.0,
        last_frame_time: std::time::Instant::now() - std::time::Duration::from_secs(1),

    }
}

/// Launch eframe app 
pub fn run_app(options: eframe::NativeOptions) -> Result<(), AppError> {
    let config = AppConfig::load();

    if !config.is_relay_tailscale {
        eframe::run_native(
            "Station",
            options,
            Box::new(move |_cc| Ok(Box::new(build_app(config)))),
        )
        .map_err(AppError::Eframe)
    } else {
        let handles = udp_tailscale_redirect(config);
        for h in handles {
            let _ = h.join();
        }
        Ok(())
    }
}

// IMPORTANT : `winit` as to match versions in every crate (egui) [cargo tree -i winit]
#[cfg(target_os = "android")]
use winit::platform::android::activity::AndroidApp;

use crate::{camera::{CameraConfig, camera_decode_thread}, control::{ActiveSource, DriveCommand, MiscellaneousCommand, ModeState, SafetyCommand, control_thread_spawn}, odometry::{mpu::spawn_integration_thread, odo::{OdoState, spawn_odo_thread}, slam::SlamState, slam_compare::{MatchAlgo, spawn_compare_thread}}, ota::serve_firmware, recorder::{recorder_init, replay_recording}, sensors::{SensorKey, lidar::{LidarDiagnostics, spawn_lidar_accumulator_thread}}, udp::{udp_cfg::{SharedCamCfg, udp_cfg_server_init}, udp_tailscale::udp_tailscale_redirect}};

#[cfg(target_os = "android")]
#[unsafe(no_mangle)]
pub fn android_main(app: AndroidApp) {
    android_logger::init_once(
        android_logger::Config::default().with_max_level(log::LevelFilter::Info),
    );

    let options = eframe::NativeOptions {
        android_app: Some(app),
        renderer: eframe::Renderer::Glow,
        ..Default::default()
    };

    if let Err(e) = run_app(options) {
        log::error!("run_app a échoué: {:?}", e);
    }
}