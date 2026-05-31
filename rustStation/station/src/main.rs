use std::{any::Any, collections::VecDeque, error::Error, sync::{Arc, atomic::AtomicBool, mpsc}, time::Instant};

use crate::{config::AppConfig, error::AppError, gui::{MyApp, Screens, ScreensTypes, screens::{camera::CameraScreen, main::MainScreen}}, udp::{udp_dump::udp_server_dump_init, udp_logs::udp_logs_server_init, udp_sensors::udp_sensors_server_init, udp_video::udp_server_video_init}};

use gui::screens::{
    commands::CommandsScreen,
    home::HomeScreen,
    logs::LogsScreen,
    sensors::SensorsScreen,
};

mod gui;
mod sensors;
mod udp;
mod error;
mod controller;
mod config;

fn main() -> Result<(), AppError> {

    env_logger::init();

    let config = AppConfig::load();

    let (tx_sensors, rx_sensors) = mpsc::channel();
    let (tx_logs, rx_logs) = mpsc::channel();
    let (tx_ctrl, rx_ctrl) = mpsc::channel();
    let (tx_img, rx_img) = mpsc::channel();
    let (tx_dump, rx_dump) = mpsc::channel();

    let sensors_connected = Arc::new(AtomicBool::new(false));
    let logs_connected = Arc::new(AtomicBool::new(false));
    let controller_connected = Arc::new(AtomicBool::new(false));
    let camera_connected = Arc::new(AtomicBool::new(false));

    let handle_ctrl = controller::init_controller(tx_ctrl, 
        Arc::clone(&controller_connected));

    let config_udp_sensors = config.clone();
    let handle_udp_sensors = udp_sensors_server_init(
        tx_sensors,
        Arc::clone(&sensors_connected),
        config_udp_sensors,
    );

    let config_udp_logs = config.clone();
    let handle_udp_logs = udp_logs_server_init(
        tx_logs,
        Arc::clone(&logs_connected),
        config_udp_logs,
    );

    let config_udp_dump = config.clone();
    let handle_udp_dump = udp_server_dump_init(
        tx_dump,
        config_udp_dump,
    );

    let config_udp_vid = config.clone();
    let handle_udp_vid = udp_server_video_init(
        tx_img,
        Arc::clone(&camera_connected),
        config_udp_vid,
    );

    let config_egui = config.clone();
    let frame = eframe::run_native(
        "Station",
        eframe::NativeOptions::default(),
        Box::new(|_cc| Box::new(MyApp {
            
            data: VecDeque::new(),
            frame: None,
            logs: VecDeque::new(),
            start: Instant::now(),
            screen: ScreensTypes::Home,
            dumps: Vec::new(),

            screens : Screens::default(),

            logs_connected,
            sensors_connected,
            controller_connected,
            camera_connected,
            
            rx_sensors,
            rx_ctrl,
            rx_logs,
            rx_frames: rx_img,
            rx_dump,

            config_egui,

            })),
    );

    handle_udp_dump.join()?;
    handle_udp_sensors.join()?;
    handle_udp_logs.join()?;
    handle_udp_vid.join()?;
    handle_ctrl.join()?;
    Ok(())
}