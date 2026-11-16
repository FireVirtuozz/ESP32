use std::{collections::VecDeque, num::NonZero, sync::{Arc, RwLock}};
use crossbeam_channel::Receiver;
use kiddo::{ImmutableKdTree, KdTree, dist::SquaredEuclidean};
use nalgebra::{Isometry2, Matrix3, Point2, RowVector3, Vector2, Vector3};

use crate::odometry::{frames::Pose, odo::OdoState};

pub enum SlamEvent {
    LidarScan {
        points: Vec<Point2<f32>>,
        odom_pose: Pose,
        fracs: Vec<f32>,       // temps relatif de chaque point : 0 = début de tour, 1 = fin
        coverage_deg: f32,
    },
}

#[derive(Debug, Default, Clone)]
pub struct SlamState {
    pub corrected_pose: (f32, f32, f32), // x, y, yaw
    pub map_points: Vec<Point2<f32>>,
    pub occupancy_grid: OccupancyGrid,
    pub cached_grid_points: Vec<[f64; 2]>,
}

impl SlamState {
    pub fn is_occupied(&self, x: f32, y: f32) -> bool {
        for obs in &self.map_points {
            let dx = x - obs.x;
            let dy = y - obs.y;
            if (dx * dx + dy * dy) < (0.25 * 0.25) { // Seuil à 25 cm
                return true;
            }
        }
        false
    }
}

#[derive(Debug, Default, Clone)]
pub struct OccupancyGrid {
    pub resolution: f32,
    pub width: usize,
    pub height: usize,
    pub origin: Point2<f32>,
    pub cells: Vec<f32>,
}

impl OccupancyGrid {
    pub fn new(width: usize, height: usize, resolution: f32) -> Self {
        Self {
            resolution,
            width,
            height,
            origin: Point2::new(
                -(width as f32 * resolution) / 2.0,
                -(height as f32 * resolution) / 2.0,
            ),
            cells: vec![0.0; width * height],
        }
    }

    pub fn world_to_map(&self, p: Point2<f32>) -> Option<(usize, usize)> {
        let mx = ((p.x - self.origin.x) / self.resolution) as i32;
        let my = ((p.y - self.origin.y) / self.resolution) as i32;
        if mx >= 0 && mx < self.width as i32 && my >= 0 && my < self.height as i32 {
            Some((mx as usize, my as usize))
        } else {
            None
        }
    }

    pub fn update_cell(&mut self, p: Point2<f32>, log_odds_delta: f32) {
        if let Some((x, y)) = self.world_to_map(p) {
            let idx = y * self.width + x;
            self.cells[idx] = (self.cells[idx] + log_odds_delta).clamp(-3.0, 3.0);
        }
    }

    pub fn update_cell_by_index(&mut self, x: usize, y: usize, log_odds_delta: f32) {
        if x < self.width && y < self.height {
            let idx = y * self.width + x;
            self.cells[idx] = (self.cells[idx] + log_odds_delta).clamp(-3.0, 3.0);
        }
    }
}

/// Update grid raycasting given a pose & a scan
pub fn update_grid_raycasting(grid: &mut OccupancyGrid, robot_pose: Pose, scan: &[Point2<f32>]) {
    let sensor_origin = robot_pose.get_translation_point();

    for &p_sensor in scan {
        let p_world = *robot_pose * p_sensor;

        if let (Some((x0, y0)), Some((x1, y1))) = (grid.world_to_map(sensor_origin), grid.world_to_map(p_world)) {
            let mut x = x0 as i32;
            let mut y = y0 as i32;
            let dx = (x1 as i32 - x0 as i32).abs();
            let dy = (y1 as i32 - y0 as i32).abs();
            let sx = if x0 < x1 { 1 } else { -1 };
            let sy = if y0 < y1 { 1 } else { -1 };
            let mut err = dx - dy;

            while x != x1 as i32 || y != y1 as i32 {
                if x >= 0 && y >= 0 {
                    grid.update_cell_by_index(x as usize, y as usize, -0.4);
                }
                let e2 = 2 * err;
                if e2 > -dy { err -= dy; x += sx; }
                if e2 < dx { err += dx; y += sy; }
            }

            grid.update_cell(p_world, 0.85);
        }
    }
}