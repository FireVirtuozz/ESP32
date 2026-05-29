use std::{collections::VecDeque, sync::{Arc, atomic::AtomicBool, mpsc}, time::Instant};

use egui::Vec2b;
use egui_plot::{Line, Plot, PlotBounds, PlotPoints};
use log::debug;

use crate::{config::AppConfig, controller::ControllerPacket, gui::screens::{camera::CameraScreen, commands::CommandsScreen, home::HomeScreen, logs::LogsScreen, main::MainScreen, sensors::SensorsScreen}, monitor::{LogPacket, TelemetryPacket}};

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
}

pub struct MyApp {
    pub rx: mpsc::Receiver<TelemetryPacket>,
    pub data: VecDeque<(TelemetryPacket, f64)>,
    pub logs: VecDeque<LogPacket>,
    pub frame: Option<Vec<u8>>,
    pub start: Instant,
    pub screen: ScreensTypes,

    pub sensors_screen: SensorsScreen,
    pub commands_screen: CommandsScreen,
    pub home_screen: HomeScreen,
    pub logs_screen: LogsScreen,
    pub main_screen: MainScreen,
    pub camera_screen: CameraScreen,

    pub logs_connected: Arc<AtomicBool>,
    pub sensors_connected: Arc<AtomicBool>,
    pub controller_connected: Arc<AtomicBool>,
    pub camera_connected: Arc<AtomicBool>,

    pub rx_ctrl: mpsc::Receiver<ControllerPacket>,
    pub rx_logs: mpsc::Receiver<LogPacket>,
    pub rx_frames: mpsc::Receiver<Vec<u8>>,

    pub config_egui: AppConfig,
}

impl eframe::App for MyApp {

    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        
        while let Ok(packet) = self.rx.try_recv() {
            let t = self.start.elapsed().as_secs_f64();
            self.data.push_back((packet, t));
            if self.data.len() > 1000 {
                self.data.pop_front();
            }
        }

        while let Ok(packet) = self.rx_logs.try_recv() {
            self.logs.push_back(packet);
            if self.logs.len() > 1000 {
                self.logs.pop_front();
            }
        }

        

        while let Ok(packet) = self.rx_frames.try_recv() {
            debug!("image received ({}) header: {:02X} {:02X} {:02X} {:02X}", 
                packet.len(),
                packet.get(0).unwrap_or(&0),
                packet.get(1).unwrap_or(&0),
                packet.get(2).unwrap_or(&0),
                packet.get(3).unwrap_or(&0),
            );
            self.frame = Some(packet);
        }

        match self.screen {
            ScreensTypes::Sensors => self.sensors_screen.show(ctx, &self.data),
            ScreensTypes::Commands => self.commands_screen.show(ctx, &self.controller_connected, &self.rx_ctrl, &mut self.screen),
            ScreensTypes::Home => self.home_screen.show(ctx, &mut self.screen),
            ScreensTypes::Logs => self.logs_screen.show(ctx, &self.logs),
            ScreensTypes::Main => self.main_screen.show(ctx,
                &mut self.screen, &self.sensors_connected, &self.logs_connected, &self.camera_connected),
            ScreensTypes::Camera => self.camera_screen.show(ctx, &mut self.frame, &self.config_egui),
        }

        ctx.request_repaint();
    }
}