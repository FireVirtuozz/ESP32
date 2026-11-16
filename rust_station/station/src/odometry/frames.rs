use std::{f32::consts::{FRAC_PI_2, PI}, ops::{Add, Deref, DerefMut, Mul, Sub}};
use nalgebra::{Isometry2, Point2, Translation, Translation2, UnitComplex, Vector2};

// Using 2D coordinates. Height ignored for now.

pub const CAR_H: f32 = 0.438;
pub const CAR_W : f32 =  0.185;

/// Pose of the car on the map
#[derive(Clone, Copy, Debug, Default)]
pub struct Pose(Isometry2<f32>);

pub struct Step {
    pub distance: f32,
    pub dyaw: f32,
}

#[derive(Clone, Copy, Debug)]
pub struct Mount {
    pub x: f32,
    pub y: f32,
    pub yaw: f32,
}

impl Mount {
    pub const fn new(x: f32, y: f32, yaw: f32) -> Self {
        Self { x, y, yaw }
    }

    pub fn iso(self) -> Isometry2<f32> {
        Isometry2::new(Vector2::new(self.x, self.y), self.yaw)
    }
}

pub const TOF: Mount = Mount::new(CAR_H / 2.0, 0.0, 0.0);
pub const LIDAR: Mount = Mount::new(0.05, 0.0, FRAC_PI_2);
pub const HC_REAR: Mount = Mount::new(-CAR_H / 2.0, 0.0, PI);
pub const HC_FRONT: Mount = Mount::new(CAR_H / 2.0, 0.0, 0.0);

impl Pose {
    pub fn new(x: f32, y: f32, yaw: f32) -> Self {
        Self(Isometry2::new(Vector2::new(x, y), yaw))
    }

    pub fn forward(&self) -> Vector2<f32> {
        self.0.rotation * Vector2::new(1.0, 0.0)
    }

    pub fn side(&self) -> Vector2<f32> {
        self.0.rotation * Vector2::new(0.0, 1.0)
    }

    pub fn on_map(self, mount: Mount, p: Point2<f32>) -> Point2<f32> {
        (self.0 * mount.iso()) * p
    }

    pub fn tof_point(self, range_m: f32) -> Point2<f32> {
        self.on_map(TOF, Point2::new(range_m, 0.0))
    }

    pub fn lidar_point(self, p: Point2<f32>) -> Point2<f32> {
        self.on_map(LIDAR, p)
    }

    pub fn update_rotation(&mut self, yaw: f32) {
        self.0.rotation = UnitComplex::new(yaw);
    }

    pub fn update_translation(&mut self, point: Point2<f32>) {
        self.0.translation = Translation { vector: Vector2::new(point.x, point.y) }
    }

    /// Gets the rotation between ]-pi;pi]
    pub fn get_rotation(&self) -> f32 {
        self.0.rotation.angle()
    }

    pub fn get_translation_point(&self) -> Point2<f32> {
        Point2::new(self.0.translation.x, self.0.translation.y)
    }

    pub fn get_translation_vector(&self) -> Vector2<f32> {
        self.0.translation.vector
    }

    pub fn inverse(&self) -> Self {
        Pose(self.0.inverse())
    }
}

impl Add<Step> for Pose {
    type Output = Self;
    fn add(self, step: Step) -> Self {
        Self(self.0 * Isometry2::new(Vector2::new(step.distance, 0.0), step.dyaw))
    }
}

impl Sub<Pose> for Pose {
    type Output = Pose;

    fn sub(self, rhs: Pose) -> Self::Output {
        Pose(rhs.0.inverse() * self.0)
    }
}

impl Mul<Pose> for Pose {
    type Output = Pose;

    fn mul(self, rhs: Pose) -> Self::Output {
        Pose(self.0 * rhs.0)
    }
}

impl Deref for Pose {
    type Target = Isometry2<f32>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for Pose {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}