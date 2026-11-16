use std::{collections::VecDeque, sync::{Arc, RwLock}};

use nalgebra::{Point2, Vector2};

use crate::{odometry::frames::{Pose, Step}, sensors::{TelemetryEnum, TelemetryPacket}};

#[derive(Debug, Default)]
pub struct OdoState {
    pub car_pose: Pose,
    pub trajectory: Arc<VecDeque<Point2<f32>>>,
    pub obstacles: Arc<VecDeque<Point2<f32>>>,
}

pub fn spawn_odo_thread(
    rx_odo: crossbeam_channel::Receiver<TelemetryPacket>,
    shared_odo: Arc<RwLock<OdoState>>,
) -> std::thread::JoinHandle<()> {
    std::thread::spawn(move || {
        let mut car_pose = Pose::default();
        let mut trajectory = VecDeque::new();
        let mut obstacles = VecDeque::new();

        while let Ok(packet) = rx_odo.recv() {
            match &packet.packet {
                TelemetryEnum::BNO085ROTATION(pck) => {
                    car_pose.update_rotation(-pck.get_angles_rad().1);
                    shared_odo.write().unwrap().car_pose = car_pose;
                }
                TelemetryEnum::KY033(ky) => {
                    let d = ky.get_distance_m() as f32;
                    let dist = if ky.sign_motor_positive { d } else { -d };
                    let step = Step {distance: dist, dyaw: 0.0};
                    car_pose = car_pose + step;
                    trajectory.push_back(car_pose.get_translation_point());
                    if trajectory.len() > 2000 { trajectory.pop_front(); }

                    //avoid doing a clone during RwLock taken (optimizable with arcswap)
                    let new_traj_arc = Arc::new(trajectory.clone());

                    let mut guard = shared_odo.write().unwrap();
                    guard.car_pose = car_pose;
                    guard.trajectory = new_traj_arc;
                }
                TelemetryEnum::VL53L1X(tof) if tof.status == 0 => {
                    let tof_m = tof.distance as f32 / 1000.0;
                    let point = car_pose.tof_point(tof_m);

                    obstacles.push_back(point);
                    if obstacles.len() > 500 { obstacles.pop_front(); }

                    let new_obstacles_arc = Arc::new(obstacles.clone());

                    let mut guard = shared_odo.write().unwrap();
                    guard.obstacles = new_obstacles_arc;
                }
                _ => {}
            }
        }
    })
}