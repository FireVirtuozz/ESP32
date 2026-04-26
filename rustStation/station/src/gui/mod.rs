use std::{collections::VecDeque, sync::{Arc, atomic::AtomicBool, mpsc}, time::Instant};

use egui::Vec2b;
use egui_plot::{Line, Plot, PlotBounds, PlotPoints};

use crate::{controller::ControllerPacket, gui::screens::{commands::CommandsScreen, home::HomeScreen, logs::LogsScreen, main::MainScreen, sensors::SensorsScreen}, monitor::TelemetryPacket};

pub mod screens;

#[derive(PartialEq)]
pub enum ScreensTypes {
    Home,
    Main,
    Sensors,
    Logs,
    Commands,
}

pub struct MyApp {
    pub rx: mpsc::Receiver<TelemetryPacket>,
    pub data: VecDeque<(TelemetryPacket, f64)>,
    pub start: Instant,
    pub screen: ScreensTypes,
    pub sensors_screen: SensorsScreen,
    pub commands_screen: CommandsScreen,
    pub home_screen: HomeScreen,
    pub logs_screen: LogsScreen,
    pub main_screen: MainScreen,
    pub logs_connected: Arc<AtomicBool>,
    pub sensors_connected: Arc<AtomicBool>,
    pub rx_ctrl: mpsc::Receiver<ControllerPacket>,
    pub controller_connected: Arc<AtomicBool>,
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

        match self.screen {
            ScreensTypes::Sensors => self.sensors_screen.show(ctx, &self.data),
            ScreensTypes::Commands => self.commands_screen.show(ctx, &self.controller_connected, &self.rx_ctrl),
            ScreensTypes::Home => self.home_screen.show(ctx, &mut self.screen),
            ScreensTypes::Logs => self.logs_screen.show(ctx),
            ScreensTypes::Main => self.main_screen.show(ctx, &mut self.screen, &self.sensors_connected, &self.logs_connected),
        }

        ctx.request_repaint();
    }
}