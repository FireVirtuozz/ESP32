use std::{collections::VecDeque, sync::{mpsc, Arc, atomic::AtomicBool}, time::Instant};

pub mod config;
pub mod controller;
pub mod error;
pub mod gui;
pub mod sensors;
pub mod udp;
pub mod recorder;

use config::AppConfig;
use error::AppError;
use gui::{MyApp, Screens, ScreensTypes};
use udp::{
    udp_dump::udp_server_dump_init, udp_logs::udp_logs_server_init,
    udp_sensors::udp_sensors_server_init, udp_video::udp_server_video_init,
};

/// Construit l'état initial de MyApp + lance tous les threads d'arrière-plan
/// (UDP sensors/logs/dump/vidéo + manette).
///
/// Commun à main.rs (desktop) et android_main (lib.rs côté Android) :
/// c'est exactement la logique qui était dans le main() d'origine, sortie
/// d'ici pour être appelable des deux côtés.
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

    // Sur Android on ne lance pas le thread manette : pas de backend gilrs
    // standard (X11/winit-gamepad) dispo, et gilrs::Gilrs::new() paniquerait.
    // À remplacer plus tard par les API InputDevice d'Android si tu veux
    // gérer une manette Bluetooth sur tel/tablette.
    #[cfg(not(target_os = "android"))]
    {
        let _handle_ctrl = controller::init_controller(
            tx_ctrl, 
            Arc::clone(&controller_connected),
            start_instant,
        );
    }
    #[cfg(target_os = "android")]
    {
        let _ = tx_ctrl; // évite le unused, le sender est juste droppé
    }

    if config.replay {
        let _handle_replay_sensor = replay_recording(tx_sensors, config.clone());
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

/// Lance eframe avec les options fournies (desktop ou android_app: Some(..)).
pub fn run_app(options: eframe::NativeOptions) -> Result<(), AppError> {
    let config = AppConfig::load();

    eframe::run_native(
        "Station",
        options,
        Box::new(move |_cc| Ok(Box::new(build_app(config)))),
    )
    .map_err(AppError::Eframe)
}

// IMPORTANT : `winit` est désormais déclaré explicitement dans Cargo.toml
// (même version que celle résolue par eframe/egui-winit, cf. `cargo tree -i winit`),
// avec la feature "android-native-activity" activée. Cela garantit qu'il
// n'existe qu'UNE seule instance de winit dans tout l'arbre de compilation,
// partagée par eframe et par notre propre code.
//
// À partir d'eframe 0.34, l'API a changé par rapport à 0.27 : NativeOptions
// expose désormais directement un champ `android_app: Option<AndroidApp>`,
// lu en interne par eframe (native/run.rs::create_event_loop) qui appelle
// lui-même `.with_android_app(...)` sur l'EventLoopBuilder. Il ne faut donc
// PLUS passer par `event_loop_builder` pour ça (c'était l'API 0.27) : il
// suffit de renseigner `android_app: Some(app)`.
#[cfg(target_os = "android")]
use winit::platform::android::activity::AndroidApp;

use crate::recorder::{recorder_init, replay_recording};

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