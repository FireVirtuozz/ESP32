use std::{collections::VecDeque, sync::{mpsc, Arc, atomic::AtomicBool}, time::Instant};

pub mod config;
pub mod controller;
pub mod error;
pub mod gui;
pub mod sensors;
pub mod udp;
pub mod recorder;
pub mod ota;
pub mod ai;

use config::AppConfig;
use error::AppError;
use gui::{MyApp, Screens, ScreensTypes};
use udp::{
    udp_dump::udp_server_dump_init, udp_logs::udp_logs_server_init,
    udp_sensors::udp_sensors_server_init, udp_video::udp_server_video_init,
};

pub fn build_app(config: AppConfig) -> MyApp {
    let (tx_sensors, rx_sensors) = mpsc::channel();
    let (tx_logs, rx_logs) = mpsc::channel();
    let (tx_ctrl, rx_ctrl) = mpsc::channel();
    let (tx_img, rx_img) = mpsc::channel();
    let (tx_dump, rx_dump) = mpsc::channel();
    let (tx_record, rx_record) = mpsc::channel();

    let sensors_connected = Arc::new(AtomicBool::new(false));
    let logs_connected = Arc::new(AtomicBool::new(false));
    let controller_connected = Arc::new(AtomicBool::new(false));
    let camera_connected = Arc::new(AtomicBool::new(false));

    let start_instant = Instant::now();

    // No controller for Android
    #[cfg(not(target_os = "android"))]
    {
        if !config.is_relay_tailscale {
            let _handle_ctrl = controller::init_controller(
                tx_ctrl, 
                Arc::clone(&controller_connected),
                start_instant,
            );
        }
    }
    #[cfg(target_os = "android")]
    {
        let _ = tx_ctrl; // sender dropped as no controller
    }

    if config.replay {
        let _handle_replay_sensor = replay_recording(tx_sensors, config.clone(), Arc::clone(&sensors_connected));
    } else {
        let _handle_udp_sensors = udp_sensors_server_init(
            tx_sensors,
            Arc::clone(&sensors_connected),
            config.clone(),
            start_instant,
            tx_record,
        );
    }

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

    if config.recording {
        let _handle_record = recorder_init(
            rx_record,
            config.clone(),
        );
    }
    
    MyApp {
        data: VecDeque::new(),
        frame: None,
        logs: VecDeque::new(),
        start: Instant::now(),
        screen: ScreensTypes::Home,
        dumps: Vec::new(),

        screens: Screens::default(),

        logs_connected,
        sensors_connected,
        controller_connected,
        camera_connected,

        rx_sensors,
        rx_ctrl,
        rx_logs,
        rx_frames: rx_img,
        rx_dump,

        config_egui: config,
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
        build_app(config);
        Ok(())
    }
}

// IMPORTANT : `winit` as to match versions in every crate (egui) [cargo tree -i winit]
#[cfg(target_os = "android")]
use winit::platform::android::activity::AndroidApp;

use crate::{ota::serve_firmware, recorder::{recorder_init, replay_recording}};

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