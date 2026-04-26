use std::{any::Any, collections::VecDeque, error::Error, sync::{Arc, atomic::AtomicBool, mpsc}, time::Instant};

use crate::{error::AppError, gui::{MyApp, ScreensTypes, screens::main::MainScreen}};

use gui::screens::{
    commands::CommandsScreen,
    home::HomeScreen,
    logs::LogsScreen,
    sensors::SensorsScreen,
};

mod gui;
mod monitor;
mod udp;
mod error;
mod controller;

fn main() -> Result<(), AppError> {

    let (tx, rx) = mpsc::channel();

    let (tx_logs, rx_logs) = mpsc::channel();

    let (tx_ctrl, rx_ctrl) = mpsc::channel();

    let sensors_connected = Arc::new(AtomicBool::new(false));
    let logs_connected = Arc::new(AtomicBool::new(false));
    let controller_connected = Arc::new(AtomicBool::new(false));

    let handle_ctrl = controller::init_controller(tx_ctrl, 
        Arc::clone(&controller_connected));

    let handle_udp = udp::udp_server_init(
        tx,
        tx_logs,
        Arc::clone(&sensors_connected),
        Arc::clone(&logs_connected),
    );

    let frame = eframe::run_native(
        "Station",
        eframe::NativeOptions::default(),
        Box::new(|_cc| Box::new(MyApp {
            rx,
            data: VecDeque::new(),
            start: Instant::now(),
            screen: ScreensTypes::Home,
            sensors_screen: SensorsScreen::default(),
            commands_screen: CommandsScreen::default(),
            home_screen: HomeScreen,
            logs_screen: LogsScreen,
            main_screen: MainScreen,
            logs_connected,
            sensors_connected,
            rx_ctrl,
            controller_connected,
            })),
    );

    handle_udp.join()?;
    handle_ctrl.join()?;
    Ok(())
}