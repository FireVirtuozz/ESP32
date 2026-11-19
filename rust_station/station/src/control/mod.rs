use std::{collections::HashMap, dbg, f64::consts::PI, fmt::{self, Display}, format, net::UdpSocket, println, sync::{Arc, RwLock, atomic::{AtomicBool, AtomicU8, AtomicU32, Ordering}}, thread, time::{Duration, Instant}, write};

use arc_swap::ArcSwap;
use crossbeam_channel::{Receiver, select, tick};
use log::error;
use nalgebra::Point2;

use crate::{config::AppConfig, control::ControlType::Gamepad, error::AppError, gui::screens::car, odometry::slam::SlamState, udp::udp_hello::EspPeer};

pub const WHEELBASE: f32 = 0.267;
pub const MAX_STEERING_DEG: f64 = 30.0;
pub const MAX_STEERING_RAD: f64 = MAX_STEERING_DEG * PI / 180.0;

pub fn generate_predicted_arc(
    car_pos: Point2<f32>,
    car_yaw: f32,
    steering_angle: f32,
    lookahead_dist: f32,
) -> Vec<[f64; 2]> {
    let mut points = Vec::new();
    
    // Courbure kappa = tan(delta) / wheelbase
    let kappa = steering_angle.tan() / WHEELBASE;
    
    let mut x = car_pos.x as f64;
    let mut y = car_pos.y as f64;
    let mut theta = car_yaw as f64;

    points.push([x, y]);

    // On trace l'arc sur la distance de lookahead en 20 petits segments
    let steps = 20;
    let ds = lookahead_dist as f64 / steps as f64;

    for _ in 0..steps {
        x += theta.cos() * ds;
        y += theta.sin() * ds;
        theta += kappa as f64 * ds; // Intégration de la trajectoire
        points.push([x, y]);
    }

    points
}

pub fn compute_pure_pursuit(
    car_pos: Point2<f32>,
    car_yaw: f32,
    path: &[[f64; 2]],
    lookahead_dist: f32,
) -> f32 {
    if path.is_empty() {
        return 0.0;
    }

    let target = path[0];
    let target_pt = Point2::new(target[0] as f32, target[1] as f32);

    let dx = target_pt.x - car_pos.x;
    let dy = target_pt.y - car_pos.y;
    
    let cos_yaw = car_yaw.cos();
    let sin_yaw = car_yaw.sin();

    let local_x = dx * cos_yaw + dy * sin_yaw;
    let local_y = -dx * sin_yaw + dy * cos_yaw;

    if local_x <= 0.0 {
        return 0.0;
    }

    let curvature = (2.0 * local_y) / (lookahead_dist * lookahead_dist);
    (curvature * WHEELBASE).atan()
}

#[derive(Debug, Clone, Copy, Default)]
pub struct Bounded<const MIN: i16, const MAX: i16>(i16);

impl<const MIN: i16, const MAX: i16> Bounded<MIN, MAX> {
    pub fn new(val: i16) -> Self { Self(val.clamp(MIN, MAX)) }
    pub fn get(&self) -> i16 { self.0 }
}

impl<const MIN: i16, const MAX: i16> fmt::Display for Bounded<MIN, MAX> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

pub struct ActiveSource(AtomicU8);

impl ActiveSource {
    pub fn set(&self, ctrl_type: ControlType) { self.0.store(ctrl_type.to_u8(), Ordering::Relaxed); }
    pub fn get(&self) -> ControlType { ControlType::from_u8(self.0.load(Ordering::Relaxed)) }
}

impl Default for ActiveSource {
    fn default() -> Self {
        Self(AtomicU8::new(ControlType::default().to_u8()))
    }
}

pub struct ModeState(AtomicU8);

impl ModeState {
    pub fn new() -> Self {
        Self(AtomicU8::new(0))
    }

    pub fn increment(&self) {
        self.0.fetch_update(Ordering::Relaxed, Ordering::Relaxed, |m| {
            if m >= 3 {
                Some(3)
            } else {
                Some(m + 1)
            }
        }).ok();
    }

    pub fn decrement(&self) {
        self.0.fetch_update(Ordering::Relaxed, Ordering::Relaxed, |m| {
            if m <= 0 {
                Some(0)
            } else {
                Some(m - 1)
            }
        }).ok();
    }

    pub fn get(&self) -> DriveMode {
        match self.0.load(Ordering::Relaxed) {
            0 => DriveMode::Default,
            1 => DriveMode::Middle,
            2 => DriveMode::Advanced,
            _ => DriveMode::Expert,
        }
    }
}

#[derive(Debug, Clone)]
pub enum DriveMode {
    Default = 0,
    Middle = 1,
    Advanced = 2,
    Expert = 3,
}

impl DriveMode {
    pub fn mode_factor(&self) -> f32 {
        match self {
            DriveMode::Default => 0.25,
            DriveMode::Middle => 0.4,
            DriveMode::Advanced => 0.7,
            DriveMode::Expert => 1.0,
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct DriveCommand {
    pub throttle: Bounded<-1000, 1000>,
    pub steering: Bounded<-1000, 1000>,
}

#[derive(Debug, Clone, Default)]
pub struct SafetyCommand {
    pub hc_block: bool,
    pub brake_stop: bool,
}

#[derive(Debug, Clone, Default)]
pub enum DriveModeCommand {
    Raw,
    #[default]
    Interp,
    Speed,
}

impl DriveModeCommand {
    pub fn to_u8(&self) -> u8 {
        match self {
            DriveModeCommand::Raw => 0,
            DriveModeCommand::Interp => 1,
            DriveModeCommand::Speed => 2,
        }
    }

    pub fn to_str(&self) -> &str {
        match self {
            DriveModeCommand::Raw => "Raw",
            DriveModeCommand::Interp => "Interp", 
            DriveModeCommand::Speed => "Speed",
        }
    }
}

impl Display for DriveModeCommand {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.to_str())
    }
}

#[derive(Debug, Clone, Default)]
pub struct MiscellaneousCommand {
    pub horn: bool,
    pub drive_mode: DriveModeCommand,
    pub target_m_s: f32,
}

pub fn to_bytes(safety_command: &SafetyCommand, drive_command: &DriveCommand, 
    start_instant: Instant, misc_command: &MiscellaneousCommand) -> [u8; 16] {
    let mut buf = [0u8; 16];
    let timestamp = start_instant.elapsed().as_millis() as u32;
    buf[0..2].copy_from_slice(&drive_command.throttle.get().to_le_bytes());
    buf[2..4].copy_from_slice(&drive_command.steering.get().to_le_bytes());
    buf[4] = safety_command.brake_stop as u8;
    buf[5] = safety_command.hc_block as u8;
    buf[6] = misc_command.horn as u8;
    buf[7] = misc_command.drive_mode.to_u8();
    buf[8..12].copy_from_slice(&misc_command.target_m_s.to_le_bytes());  
    buf[12..16].copy_from_slice(&timestamp.to_le_bytes());
    buf
}

#[derive(Debug)]
pub enum ControlType {
    Gamepad(DriveCommand),
    Android(DriveCommand),
    Ai(DriveCommand),
    Waypoints(DriveCommand),
}

impl Default for ControlType {
    fn default() -> Self {
        Self::Gamepad(DriveCommand::default())
    }
}

impl ControlType {

    pub fn into_drive_command(&self) -> DriveCommand {
        match self {
            ControlType::Ai(cmd) => cmd.clone(),
            ControlType::Android(cmd) => cmd.clone(),
            ControlType::Gamepad(cmd) => cmd.clone(),
            ControlType::Waypoints(cmd) => cmd.clone(),
        }
    }

    pub fn to_u8(&self) -> u8 {
        match self {
            ControlType::Gamepad(_) => 0,
            ControlType::Android(_) => 1,
            ControlType::Ai(_) => 2,
            ControlType::Waypoints(_) => 3,
        }
    }

    pub fn from_u8(val : u8) -> ControlType {
        let drv = DriveCommand::default();
        match val {
            0 => ControlType::Gamepad(drv),
            1 => ControlType::Android(drv),
            2 => ControlType::Ai(drv),
            3 => ControlType::Waypoints(drv),
            _ => ControlType::default(),
        }
    }
}


pub fn control_thread_spawn(
    rx_control: Receiver<ControlType>,
    rx_safety: Receiver<SafetyCommand>,
    rx_misc: Receiver<MiscellaneousCommand>,
    config_udp_control: AppConfig,
    start_instant: Instant,
    active_source: Arc<ActiveSource>,
    packet_sent: Arc<AtomicU32>,
    active_drive: Arc<ArcSwap<DriveCommand>>,
    active_safety: Arc<ArcSwap<SafetyCommand>>,
    active_misc: Arc<ArcSwap<MiscellaneousCommand>>,
    shared_waypoints: Arc<RwLock<Vec<[f64; 2]>>>,
    shared_slam: Arc<RwLock<SlamState>>,
    shared_predicted_path: Arc<RwLock<Vec<[f64; 2]>>>,
    shared_esps: Arc<RwLock<HashMap<u8, EspPeer>>>,
) -> thread::JoinHandle<()> {

    let handle = thread::spawn(move || {
        if let Err(e) = control_thread(
            rx_control, rx_safety, rx_misc, config_udp_control, start_instant, active_source, packet_sent,
            active_drive, active_safety, active_misc, shared_waypoints, shared_slam, shared_predicted_path,
            shared_esps
        ) {
            error!("UDP error dump: {:?}", e);
        }
    });
    handle
}

fn control_thread(
    rx_control: Receiver<ControlType>,
    rx_safety: Receiver<SafetyCommand>,
    rx_misc: Receiver<MiscellaneousCommand>,
    config_udp_control: AppConfig,
    start_instant: Instant,
    active_source: Arc<ActiveSource>,
    packet_sent: Arc<AtomicU32>,
    active_drive: Arc<ArcSwap<DriveCommand>>,
    active_safety: Arc<ArcSwap<SafetyCommand>>,
    active_misc: Arc<ArcSwap<MiscellaneousCommand>>,
    shared_waypoints: Arc<RwLock<Vec<[f64; 2]>>>,
    shared_slam: Arc<RwLock<SlamState>>,
    shared_predicted_path: Arc<RwLock<Vec<[f64; 2]>>>,
    shared_esps: Arc<RwLock<HashMap<u8, EspPeer>>>,
) -> Result<(), AppError> { 

    let socket = UdpSocket::bind("0.0.0.0:0")?;

    let rx_tick = tick(Duration::from_millis(20));

    loop {
        let mut should_send = false;

        select! {
            recv(rx_control) -> cmd => {
                if let Ok(cmd) = cmd {
                    if cmd.to_u8() == active_source.get().to_u8() {
                        let drv_cmd = cmd.into_drive_command();
                        let mut misc_cmd = (**active_misc.load()).clone();
                        misc_cmd.target_m_s = (40.0 / 3.6) * (drv_cmd.throttle.get() as f32 / 1000.0);
                        active_misc.store(Arc::new(misc_cmd));

                        active_drive.store(Arc::new(drv_cmd));
                        should_send = true;
                    }
                } else {
                    error!("rx_control closed");
                    return Ok(());
                }
            }
            recv(rx_safety) -> cmd => {
                if let Ok(cmd) = cmd {
                    active_safety.store(Arc::new(cmd));
                    should_send = true;
                } else {
                    error!("rx_safety closed");
                    return Ok(());
                }
            }
            recv(rx_misc) -> cmd => {
                if let Ok(cmd) = cmd {
                    active_misc.store(Arc::new(cmd));
                    should_send = true;
                } else {
                    error!("rx_misc closed");
                    return Ok(());
                }
            }
            recv(rx_tick) -> _ => {
                match active_source.get() {
                    ControlType::Waypoints(_) => {
                        let (x, y, yaw) = {
                            let slam = shared_slam.read().unwrap();
                            slam.corrected_pose // (x, y, yaw)
                        };
                        let car_pos = Point2::new(x, y);

                        let mut waypoints = shared_waypoints.write().unwrap();
                        if let Some(target) = waypoints.first() {
                            let target_pos = Point2::new(target[0] as f32, target[1] as f32);
                            let dist = (target_pos - car_pos).norm();

                            // Waypoint atteint (seuil 30 cm) -> on le supprime
                            if dist < 0.3 {
                                waypoints.remove(0);
                            } else {
                                // 1. Calcul de la direction idéale (Pure Pursuit)
                                let ideal_steering = compute_pure_pursuit(car_pos, yaw, &waypoints, 1.0);

                                let mut best_steering = 0.0;
                                let mut best_arc = Vec::new();
                                let mut min_cost = f32::MAX;
                                let mut path_found = false;

                                let step_deg = 3.0_f32;

                                // 2. Échantillonnage et test de collision des arcs
                                let slam = shared_slam.read().unwrap();
                                for angle_deg in (-MAX_STEERING_DEG as i32..=MAX_STEERING_DEG as i32).step_by(step_deg as usize) {
                                    let steering_rad = (angle_deg as f32).to_radians();
                                    let arc = generate_predicted_arc(car_pos, yaw, steering_rad, 1.0);

                                    let mut collision = false;
                                    for pt in &arc {
                                        if slam.is_occupied(pt[0] as f32, pt[1] as f32) {
                                            collision = true;
                                            break;
                                        }
                                    }
                                    if collision {
                                        continue;
                                    }

                                    if let Some(end_pt) = arc.last() {
                                        let end_pos = Point2::new(end_pt[0] as f32, end_pt[1] as f32);
                                        let dist_to_target = (end_pos - target_pos).norm();
                                        let steering_penalty = (steering_rad - ideal_steering).abs() * 0.8;
                                        let cost = dist_to_target + steering_penalty;

                                        if cost < min_cost {
                                            min_cost = cost;
                                            best_steering = steering_rad;
                                            best_arc = arc;
                                            path_found = true;
                                        }
                                    }
                                }
                                drop(slam); // Libération explicite du verrou SLAM

                                if path_found {
                                    let steering_val = ((best_steering / MAX_STEERING_RAD as f32) * 1000.0) as i16;

                                    let mut path = shared_predicted_path.write().unwrap();
                                    *path = best_arc;

                                    let mut drv_cmd = (**active_drive.load()).clone();
                                    drv_cmd.steering = Bounded::new(-steering_val);
                                    active_drive.store(Arc::new(drv_cmd));

                                    let max_speed = 1.2;
                                    let min_speed = 0.4;
                                    let target_speed = (dist * 2.0).clamp(min_speed, max_speed);

                                    let mut misc_cmd = (**active_misc.load()).clone();
                                    misc_cmd.target_m_s = target_speed;
                                    misc_cmd.drive_mode = DriveModeCommand::Speed;
                                    active_misc.store(Arc::new(misc_cmd));

                                    let mut cmd_safety = (**active_safety.load()).clone();
                                    cmd_safety.brake_stop = false;
                                    active_safety.store(Arc::new(cmd_safety));

                                    should_send = true;
                                } else {
                                    // AUCUN ARC LIBRE : Freinage d'urgence (mur inévitable)
                                    let mut drv_cmd = (**active_drive.load()).clone();
                                    drv_cmd.steering = Bounded::new(0);
                                    active_drive.store(Arc::new(drv_cmd));

                                    let mut misc_cmd = (**active_misc.load()).clone();
                                    misc_cmd.target_m_s = 0.0;
                                    misc_cmd.drive_mode = DriveModeCommand::Speed;
                                    active_misc.store(Arc::new(misc_cmd));

                                    let mut cmd_safety = (**active_safety.load()).clone();
                                    cmd_safety.brake_stop = true;
                                    active_safety.store(Arc::new(cmd_safety));

                                    let mut path = shared_predicted_path.write().unwrap();
                                    path.clear();

                                    should_send = true;
                                }
                            }
                        } else {
                            // PLUS DE WAYPOINTS : On stoppe la voiture
                            let mut drv_cmd = (**active_drive.load()).clone();
                            drv_cmd.steering = Bounded::new(0);
                            active_drive.store(Arc::new(drv_cmd));

                            let mut misc_cmd = (**active_misc.load()).clone();
                            misc_cmd.target_m_s = 0.0;
                            misc_cmd.drive_mode = DriveModeCommand::Speed;
                            active_misc.store(Arc::new(misc_cmd));

                            let mut cmd_safety = (**active_safety.load()).clone();
                            cmd_safety.brake_stop = true;
                            active_safety.store(Arc::new(cmd_safety));

                            let mut path = shared_predicted_path.write().unwrap();
                            path.clear();

                            should_send = true;
                        }
                    }
                    _ => {}
                }
            }
        }

        if should_send {
            //dbg!((&*active_safety.load(), &*active_drive.load(), &*active_misc.load()));
            let bytes = to_bytes(
                &*active_safety.load(), 
                &*active_drive.load(), 
                start_instant,
                &*active_misc.load(),
            );
            let esp_ref = shared_esps.read().unwrap();
            let ad = if let Some(esp) = esp_ref.get(&0) { Some(esp.ip_addr.ip()) } else { None };
            drop(esp_ref);
            if let Some(ip) = ad {
                let addr_cmd = format!("{}:3333", ip);
                if let Err(e) = socket.send_to(&bytes, &addr_cmd) {
                    error!("send control failed: {e}");
                } else {
                    packet_sent.fetch_add(1, Ordering::Relaxed);
                }
            }
        }
    }

}