use std::error::Error;

use eframe::Frame;

use crate::{error::AppError, sensors::{PacketEsp, PacketImu, PacketTemperature, PacketUltrasonic}};

pub fn parse_buffer_ina(buffer : &[u8]) -> Result<super::PacketIna, AppError> {
    let bus_voltage       = i16::from_le_bytes(buffer[0..2].try_into()?);
    let current         = u16::from_le_bytes(buffer[2..4].try_into()?);
    let power   = u16::from_le_bytes(buffer[4..6].try_into()?);
    let shunt_voltage = i16::from_le_bytes(buffer[6..8].try_into()?);

    Ok(super::PacketIna {
        current,
        power,
        bus_voltage,
        shunt_voltage,
    })
}

pub fn parse_buffer_ultrasonic(buffer : &[u8]) -> Result<super::PacketUltrasonic, AppError> {
    let duration = i64::from_le_bytes(buffer[0..8].try_into()?);

    Ok(super::PacketUltrasonic {
        duration,
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


pub fn parse_buffer_mpu(buffer : &[u8]) -> Result<super::PacketImu, AppError> {
    let accel_x       = i16::from_le_bytes(buffer[0 .. 2].try_into()?);
    let accel_y         = i16::from_le_bytes(buffer[2 .. 4].try_into()?);
    let accel_z   = i16::from_le_bytes(buffer[4 .. 6].try_into()?);
    let gyro_x       = i16::from_le_bytes(buffer[6 .. 8].try_into()?);
    let gyro_y         = i16::from_le_bytes(buffer[8 .. 10].try_into()?);
    let gyro_z   = i16::from_le_bytes(buffer[10 .. 12].try_into()?);
    let temperature_chip= i16::from_le_bytes(buffer[12 .. 14].try_into()?);
    let pressure = i32::from_le_bytes(buffer[14 .. 18].try_into()?);
    let temperature = i32::from_le_bytes(buffer[18 .. 22].try_into()?);

    Ok(super::PacketImu {
        accel_x,
        accel_y,
        accel_z,
        gyro_x,
        gyro_y,
        gyro_z,
        pressure,
        temperature,
        temperature_chip,
    })
}

pub struct SensorsUdpHeader {
    pub ftype: u8,
    pub esp_id: u8,
    pub timestamp: u32,
    //pub checksum: u16, for later use
}

//size from a manual constant, to avoid padding from struct in memory when using "sizeof"
pub const SENSORS_HEADER_SIZE: usize = 6;

impl SensorsUdpHeader {
    pub fn header_from_buffer(buf: &[u8]) -> Result<Self, AppError> {
        if buf.len() < SENSORS_HEADER_SIZE {
            return Err("Header not valid".into())
        }

        let ftype = buf[0];
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