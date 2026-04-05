use std::error::Error;

use eframe::Frame;

use crate::monitor::{PacketEsp, PacketImu, PacketTemperature, PacketUltrasonic};

pub fn parse_buffer_ina(buffer : &[u8]) -> Result<super::PacketIna, Box<dyn Error>> {
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

pub fn parse_buffer_ultrasonic(buffer : &[u8]) -> Result<super::PacketUltrasonic, Box<dyn Error>> {
    let duration = i64::from_le_bytes(buffer[0..8].try_into()?);

    Ok(super::PacketUltrasonic {
        duration,
    })
}

pub fn parse_buffer_hall(buffer : &[u8]) -> Result<super::PacketHall, Box<dyn Error>> {
    let revolution_count       = u64::from_le_bytes(buffer[0 .. 8].try_into()?);
    let revolution_duration         = i64::from_le_bytes(buffer[8 .. 16].try_into()?);

    Ok(super::PacketHall {
        revolution_count,
        revolution_duration,
    })
}


pub fn parse_buffer_mpu(buffer : &[u8]) -> Result<super::PacketImu, Box<dyn Error>> {
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

pub struct FrameUdpHeader {
    pub ftype: u8,
    pub size: u8,
    pub flags: u8,
    pub id: u32,
}

//size from a manual constant, to avoid padding from struct in memory when using "sizeof"
pub const HEADER_SIZE: usize = 7;

impl FrameUdpHeader {
    pub fn header_from_buffer(buf: &[u8]) -> Result<Self, Box<dyn Error>> {
        if buf.len() < HEADER_SIZE {
            return Err("Header not valid".into())
        }

        //example with get
        let ftype = match buf.get(0) {
            //here, using "&var" because get returns a "&u8"
            //so according to pattern, "&var" means that "var = u8"
            //otherwise, if "var" only, then "var = &u8"
            Some(&frame_type) => match frame_type {
                0 => {
                    println!("Frame valid, of type monitor");
                    frame_type
                },
                _ => return Err("Unvalid type in frame".into()),
            },
            _ => return Err("Frame not valid".into()),
        };

        let flags = buf[1];
        let size = buf[2];
        let id = u32::from_le_bytes([
            buf[3],
            buf[4],
            buf[5],
            buf[6],
        ]);

        Ok(Self {
            ftype,
            size,
            flags,
            id,
        })
    }
}