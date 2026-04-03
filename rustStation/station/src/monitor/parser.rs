use std::error::Error;

use crate::monitor::{PacketEsp, PacketImu, PacketTemperature, PacketUltrasonic};

fn parse_buffer_ina(buffer : &[u8]) -> Result<super::PacketIna, Box<dyn Error>> {
    let current       = i16::from_le_bytes(buffer[0..2].try_into()?);
    let power         = u16::from_le_bytes(buffer[2..4].try_into()?);
    let bus_voltage   = i16::from_le_bytes(buffer[4..6].try_into()?);
    let shunt_voltage = i16::from_le_bytes(buffer[6..8].try_into()?);

    Ok(super::PacketIna {
        current,
        power,
        bus_voltage,
        shunt_voltage,
    })
}

fn parse_buffer_hall(buffer : &[u8]) -> Result<super::PacketHall, Box<dyn Error>> {
    let rev_count       = u32::from_le_bytes(buffer[0 .. 4].try_into()?);
    let delta_rev         = u32::from_le_bytes(buffer[4 .. 8].try_into()?);
    let delta_t   = u32::from_le_bytes(buffer[8 .. 12].try_into()?);

    Ok(super::PacketHall {
        revolution_count: rev_count,
        delta_revolution: delta_rev,
        delta_time: delta_t,
    })
}

fn parse_buffer_telemetry(buffer : &[u8]) -> Result<super::TelemetryPacket, Box<dyn Error>> {
    let timestamp = u32::from_le_bytes(buffer[0 .. 4].try_into()?);
    let ina = parse_buffer_ina(buffer[4 .. 12].try_into()?)?;
    let hall= parse_buffer_hall(buffer[12 .. 24].try_into()?)?;

    let temperature = super::PacketTemperature {
        temperature: 0,
    };

    let imu = PacketImu {
        accel_x: 0,
        accel_y: 0,
        accel_z: 0,
        gyro_x: 0,
        gyro_y: 0,
        gyro_z: 0,
        pressure: 0,
        temperature: 0,
        temperature_chip: 0,
    };

    let ultrasonic = PacketUltrasonic {
        duration: 0,
    };

    let esp = PacketEsp {
        current_heap: 0,
        direction_value: 0,
        max_duty_direction: 0,
        max_duty_motor: 0,
        min_duty_direction: 0,
        min_duty_motor: 0,
        motor_value: 0,
        total_heap: 0,
    };

    Ok(super::TelemetryPacket {
        timestamp_ms: timestamp,
        ina,
        hall,
        imu,
        temperature,
        ultrasonic,
        esp,
    })
}