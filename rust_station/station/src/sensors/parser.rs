use core::time;
use std::{error::Error, time::Instant};

use eframe::Frame;
use serde::{Deserialize, Serialize};

use crate::{error::AppError, gui::screens::tuning::CurveType, sensors::{Bno085Classif, Bno085Status, BrakePacket, DriveMode, EspPacket, EspResetReason, PacketBmp, PacketBno085Accel, PacketBno085Classif, PacketBno085LinearAccel, PacketBno085Rotation, PacketDht11, PacketImu, PacketMotor, PacketPhotosensor, PacketPong, PacketTemperature, PacketUltrasonic, PacketVl53l1x, SensorType}};

pub fn parse_buffer_ina(buffer : &[u8]) -> Result<super::PacketIna, AppError> {
    let bus_voltage       = i16::from_le_bytes(buffer[0..2].try_into()?);
    let current         = i16::from_le_bytes(buffer[2..4].try_into()?);
    let power   = u16::from_le_bytes(buffer[4..6].try_into()?);
    let shunt_voltage = i16::from_le_bytes(buffer[6..8].try_into()?);

    Ok(super::PacketIna {
        current,
        power,
        bus_voltage,
        shunt_voltage,
    })
}

pub fn parse_buffer_ultrasonic(buffer : &[u8]) -> Result<PacketUltrasonic, AppError> {
    let hc_id = buffer[0];
    let duration = i64::from_le_bytes(buffer[1..9].try_into()?);
    let blocked = match buffer[9] {1 => true, _ => false};

    Ok(PacketUltrasonic {
        hc_id,
        duration,
        blocked
    })
}

pub fn parse_buffer_hall(buffer : &[u8]) -> Result<super::PacketHall, AppError> {
    let revolution_count       = u64::from_le_bytes(buffer[0 .. 8].try_into()?);
    let revolution_duration         = i64::from_le_bytes(buffer[8 .. 16].try_into()?);

    Ok(super::PacketHall {
        revolution_count,
        revolution_duration,
    })
}

pub fn parse_buffer_bmp(buffer : &[u8]) -> Result<PacketBmp, AppError> {
    let pressure = i32::from_le_bytes(buffer[0 .. 4].try_into()?);
    let temperature = i32::from_le_bytes(buffer[4 .. 8].try_into()?);

    Ok(PacketBmp {
        pressure,
        temperature,
    })
}


pub fn parse_buffer_mpu(buffer : &[u8]) -> Result<super::PacketImu, AppError> {
    let accel_x       = i16::from_le_bytes(buffer[0 .. 2].try_into()?);
    let accel_y         = i16::from_le_bytes(buffer[2 .. 4].try_into()?);
    let accel_z   = i16::from_le_bytes(buffer[4 .. 6].try_into()?);
    let gyro_x       = i16::from_le_bytes(buffer[6 .. 8].try_into()?);
    let gyro_y         = i16::from_le_bytes(buffer[8 .. 10].try_into()?);
    let gyro_z   = i16::from_le_bytes(buffer[10 .. 12].try_into()?);
    let temperature_chip= i16::from_le_bytes(buffer[12 .. 14].try_into()?);

    Ok(super::PacketImu {
        accel_x,
        accel_y,
        accel_z,
        gyro_x,
        gyro_y,
        gyro_z,
        temperature_chip,
    })
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct SensorsUdpHeader {
    pub ftype: SensorType,
    pub esp_id: u8,
    pub timestamp: u32,
    //pub checksum: u16, for later use
}

//size from a manual constant, to avoid padding from struct in memory when using "sizeof"
pub const SENSORS_HEADER_SIZE: usize = 4 + 1 + 1;

impl SensorsUdpHeader {
    pub fn header_from_buffer(buf: &[u8]) -> Result<Self, AppError> {
        if buf.len() < SENSORS_HEADER_SIZE {
            return Err("Header not valid".into())
        }

        let ftype = SensorType::try_from(buf[0])?;
        let esp_id = buf[1];
        let timestamp = u32::from_le_bytes([
            buf[2],
            buf[3],
            buf[4],
            buf[5],
        ]);

        Ok(Self {
            ftype,
            esp_id,
            timestamp,
        })
    }
}

pub fn parse_buffer_esp(buffer: &[u8]) -> Result<EspPacket, AppError> {
    let esp_deg          = f32::from_le_bytes(buffer[0 .. 4].try_into()?);
    let rssi             = buffer[4] as i8;
    let reset_reason     = EspResetReason::from_u8(buffer[5]);
    let free_dram        = u32::from_le_bytes(buffer[6 .. 10].try_into()?);
    let free_dram_block  = u32::from_le_bytes(buffer[10 .. 14].try_into()?);
    let free_psram       = u32::from_le_bytes(buffer[14 .. 18].try_into()?);
    let free_psram_block = u32::from_le_bytes(buffer[18 .. 22].try_into()?);
    let angle            = i16::from_le_bytes(buffer[22 .. 24].try_into()?);
    let motor            = i16::from_le_bytes(buffer[24 .. 26].try_into()?);
    let nb_packets       = u32::from_le_bytes(buffer[26 .. 30].try_into()?);
    let core0            = f32::from_le_bytes(buffer[30 .. 34].try_into()?);
    let core1            = f32::from_le_bytes(buffer[34 .. 38].try_into()?);
    let udp_drop         = u32::from_le_bytes(buffer[38 .. 42].try_into()?);
    let ip_drop          = u32::from_le_bytes(buffer[42 .. 46].try_into()?);
    let udp_chkerr       = u32::from_le_bytes(buffer[46 .. 50].try_into()?);

    Ok(EspPacket {
        esp_deg,
        rssi,
        reset_reason,
        free_dram,
        free_dram_block,
        free_psram,
        free_psram_block,
        angle,
        motor,
        nb_packets,
        core0,
        core1,
        udp_drop,
        ip_drop,
        udp_chkerr,
    })
}

pub fn parse_buffer_pong(buffer: &[u8], start_instant: Instant) -> Result<PacketPong, AppError> {
    let timestamp_pc = u32::from_le_bytes(buffer[0 .. 4].try_into()?);
    let now_ms = start_instant.elapsed().as_millis() as u32;
    let ping_pong = now_ms.wrapping_sub(timestamp_pc);
    Ok(PacketPong { 
        ping_pong,
    })
}

pub fn parse_buffer_motor(buf: &[u8]) -> Result<PacketMotor, AppError> {
    let curve_type = CurveType::from_u8(buf[0]);
    let accel_param = buf[1];
    let decel_param = buf[2];
    let current_motor = i16::from_le_bytes(buf[3 .. 5].try_into()?);
    let target_motor = i16::from_le_bytes(buf[5 .. 7].try_into()?);
    let hc_block_activated = match buf[7] {1 => true, _ => false};

    Ok(PacketMotor {
        accel_param,
        decel_param,
        curve_type,
        current_motor,
        target_motor,
        hc_block_activated,
    })
}

pub fn parse_buffer_brake(buf: &[u8]) -> Result<BrakePacket, AppError> {
    let braking = match buf[0] {1 => true, 0 => false, _ => false};
    let timeout_braking = u32::from_le_bytes(buf[1 .. 5].try_into()?);
    let pulses_100ms = u16::from_le_bytes(buf[5 .. 7].try_into()?);
    let pulses_20ms = u16::from_le_bytes(buf[7 .. 9].try_into()?);
    let current_motor = i16::from_le_bytes(buf[9 .. 11].try_into()?);

    Ok(BrakePacket {
        braking,
        current_motor,
        pulses_100ms,
        pulses_20ms,
        timeout_braking,
    })
}

pub fn parse_buffer_dht11(buf: &[u8]) -> Result<PacketDht11, AppError> {
    let humidity = buf[0];
    let temperature = buf[1];

    Ok(PacketDht11 {
        humidity,
        temperature,
    })
}

pub fn parse_buffer_photosensor(buf: &[u8]) -> Result<PacketPhotosensor, AppError> {
    let raw_value = i32::from_le_bytes(buf[0 .. 4].try_into()?);

    Ok(PacketPhotosensor {
        raw_value,
    })
}

pub fn parse_buffer_vl53l1x(buf: &[u8]) -> Result<PacketVl53l1x, AppError> {
    let distance = u16::from_le_bytes(buf[0 .. 2].try_into()?);
    let ambient = u16::from_le_bytes(buf[2 .. 4].try_into()?);
    let sig_per_spad = u16::from_le_bytes(buf[4 .. 6].try_into()?);
    let num_spad = u16::from_le_bytes(buf[6 .. 8].try_into()?);
    let status = buf[8];

    Ok(PacketVl53l1x {
        distance,
        ambient,
        sig_per_spad,
        num_spad,
        status,
    })
}

pub fn parse_buffer_bno085_accel(buf: &[u8]) -> Result<PacketBno085Accel, AppError> {
    let status = Bno085Status::from_u8(buf[0]);
    let timestamp = u32::from_le_bytes(buf[1 .. 5].try_into()?);
    let x = f32::from_le_bytes(buf[5 .. 9].try_into()?);
    let y = f32::from_le_bytes(buf[9 .. 13].try_into()?);
    let z = f32::from_le_bytes(buf[13 .. 17].try_into()?);

    Ok(PacketBno085Accel {
        status,
        timestamp,
        x,
        y,
        z,
    })
}

pub fn parse_buffer_bno085_linear_accel(buf: &[u8]) -> Result<PacketBno085LinearAccel, AppError> {
    let status = Bno085Status::from_u8(buf[0]);
    let timestamp = u32::from_le_bytes(buf[1 .. 5].try_into()?);
    let x = f32::from_le_bytes(buf[5 .. 9].try_into()?);
    let y = f32::from_le_bytes(buf[9 .. 13].try_into()?);
    let z = f32::from_le_bytes(buf[13 .. 17].try_into()?);

    Ok(PacketBno085LinearAccel {
        status,
        timestamp,
        x,
        y,
        z,
    })
}

pub fn parse_buffer_bno085_classif(buf: &[u8]) -> Result<PacketBno085Classif, AppError> {
    let status = Bno085Status::from_u8(buf[0]);
    let classif = Bno085Classif::from_u8(buf[1]);

    Ok(PacketBno085Classif {
        status,
        classif,
    })
}

pub fn parse_buffer_bno085_rotation(buf: &[u8]) -> Result<PacketBno085Rotation, AppError> {
    let status = Bno085Status::from_u8(buf[0]);
    let accuracy = f32::from_le_bytes(buf[1 .. 5].try_into()?);
    let pitch = f32::from_le_bytes(buf[5 .. 9].try_into()?);
    let roll = f32::from_le_bytes(buf[9 .. 13].try_into()?);
    let yaw = f32::from_le_bytes(buf[13 .. 17].try_into()?);

    Ok(PacketBno085Rotation {
        status,
        accuracy,
        pitch,
        roll,
        yaw,
    })
}
