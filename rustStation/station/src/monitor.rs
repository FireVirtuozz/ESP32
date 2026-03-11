use std::{error::Error, f64::consts::PI, io};

pub mod parser;

const TIRE_DIAMETER : u8 = 65; //mm

const INA_SHUNT_RESISTANCE: f64 = 0.1; //ohms on INA226
//on datasheet, shunt voltage input range [-81.92 .. 81.92]mV
const INA_MAX_CURRENT: f64 = (81.92 * 1.0e-3) / INA_SHUNT_RESISTANCE; //~800mA
const INA_REGISTER_SIZE: f64 = 32768.0; //2^15, 1 register on 16 bits (1 sign bit)
const INA_CURRENT_LSB: f64 = INA_MAX_CURRENT / INA_REGISTER_SIZE;
const INA_LSB_POWER: u8 = 25;
const INA_LSB_SHUNT_VOLTAGE: f64 = (2.5 * 1.0e-6); //2.5uV
const INA_LSB_BUS_VOLTAGE: f64 = (1.25 * 1.0e-3); //1.25mV

//KY-003
struct PacketHall {
    revolution_count: u32,
    delta_revolution: u32,
    delta_time: u32,
}

impl PacketHall {

    fn get_speed_m_s(&self) -> Result<f64, Box<dyn Error>> {

        if self.delta_time == 0 {
            //same thing, into converts automatically
            //return Err("dt is zero, division by zero".into());
            return Err(Box::<dyn Error>::from("dt is zero, division by zero"));
        }

        //infaillible so no error handling, try_from otherwise
        let revs = self.delta_revolution as f64;
        let dt = self.delta_time as f64; //cast
        let diam = TIRE_DIAMETER as f64;
        Ok((revs / dt) * PI * diam * 1.0e-3)
    }

    fn get_speed_km_h(&self) -> Result<f64, Box<dyn Error>> {
        let speed_m_s = self.get_speed_m_s()?; //error propagation
        Ok(speed_m_s * 3.6)
    }

    fn get_distance_m(&self) -> f64 {
        let revs = self.revolution_count as f64;
        let diam = TIRE_DIAMETER as f64;
        revs * PI * diam * 1.0e-3
    }

    fn get_distance_km(&self) -> f64 {
        self.get_distance_m() * 1.0e-3
    }
}

//ina226
pub struct PacketIna {
    pub current: i16,
    pub power: u16,
    pub bus_voltage: i16,
    pub shunt_voltage: i16,
}

impl PacketIna {
    fn get_current_ma(&self) -> f64 {
        let curr = self.current as f64;
        curr * INA_CURRENT_LSB * 1.0e3
    }

    fn get_power_mw(&self) -> f64 {
        let pow = self.power as f64;
        let ratio: f64 = INA_LSB_POWER as f64;
        pow * ratio * INA_CURRENT_LSB * 1.0e3
    }

    fn get_bus_voltage_mv(&self) -> f64 { //max: 40.96V
        self.bus_voltage as f64 * INA_LSB_BUS_VOLTAGE * 1.0e3
    }

    fn get_shunt_voltage_mv(&self) -> f64 { //max: 81.92mV
        self.shunt_voltage as f64 * INA_LSB_SHUNT_VOLTAGE * 1.0e3
    }

    pub fn new() -> Self {
        Self {
            current : 0,
            power : 0,
            bus_voltage : 0,
            shunt_voltage : 0,
        }
    }
}

//ov2640
struct PacketCamera {
    frame_id: u32,
    timestamp_ms: u32,
    width: u16,
    height: u16,
}

//MPU9250+BMP280
struct PacketImu { 
    accel_x: i16,
    accel_y: i16,
    accel_z: i16,
    gyro_x: i16,
    gyro_y: i16,
    gyro_z: i16,
    magne_x: i16,
    magne_y: i16,
    magne_z: i16,
    temperature_chip: i16,
    pressure: i32,
    temperature: i32,
}

//esp32
struct PacketEsp {
    total_heap: u32,
    current_heap: u32,
    motor_value: i8,
    direction_value: i8,
    max_duty_motor: u16,
    min_duty_motor: u16,
    max_duty_direction: u16,
    min_duty_direction: u16,
}

//DS18B20
struct PacketTemperature {
    temperature: i16,
}

//HC-SR04
struct PacketUltrasonic {
    duration: u32,
}

//Buffer from ESP
pub struct TelemetryPacket {
    timestamp_ms: u32,
    hall: PacketHall,
    ina: PacketIna,
    imu: PacketImu,
    temperature: PacketTemperature,
    ultrasonic: PacketUltrasonic,
    esp: PacketEsp,
}

