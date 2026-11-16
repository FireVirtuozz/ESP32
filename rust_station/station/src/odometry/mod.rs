use crate::odometry::mpu::ProcessedMpu;

pub mod mpu;
pub mod odo;
pub mod slam;
pub mod slam_compare;
pub mod frames;

pub enum ProcessedKey {
    MPU,
    LAST,
}

impl From<&ProcessedTelemetry> for ProcessedKey {
    fn from(value: &ProcessedTelemetry) -> Self {
        match value {
            ProcessedTelemetry::MPU(_) => ProcessedKey::MPU,
        }   
    }
}

impl ProcessedKey {
    pub const COUNT: usize = ProcessedKey::LAST as usize;

    pub fn index(self) -> usize {
        self as usize
    }
}

pub enum ProcessedTelemetry {
    MPU(ProcessedMpu),
}