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

pub const KY_SIZE: usize = 16;

//KY-003
#[derive(Debug)]
pub struct PacketHall {
    revolution_count: u64,
    revolution_duration: i64,
}

impl PacketHall {

    pub fn get_speed_m_s(&self) -> Result<f64, Box<dyn Error>> {

        if self.revolution_duration == 0 {
            //same thing, into converts automatically
            //return Err("dt is zero, division by zero".into());
            return Err(Box::<dyn Error>::from("dt is zero, division by zero"));
        }

        //infaillible so no error handling, try_from otherwise
        let dt = self.revolution_duration as f64; //cast
        let diam = TIRE_DIAMETER as f64;
        Ok((1.0 / dt) * PI * diam * 1.0e-3) //1 revolution per dt
    }

    pub fn get_speed_km_h(&self) -> Result<f64, Box<dyn Error>> {
        let speed_m_s = self.get_speed_m_s()?; //error propagation
        Ok(speed_m_s * 3.6)
    }

    pub fn get_distance_m(&self) -> f64 {
        let revs = self.revolution_count as f64;
        let diam = TIRE_DIAMETER as f64;
        revs * PI * diam * 1.0e-3
    }

    pub fn get_distance_km(&self) -> f64 {
        self.get_distance_m() * 1.0e-3
    }
}

pub const INA_SIZE: usize = 8;

//ina226
#[derive(Debug)]
pub struct PacketIna {
    pub current: u16,
    pub power: u16,
    pub bus_voltage: i16,
    pub shunt_voltage: i16,
}

impl PacketIna {
    pub fn get_current_ma(&self) -> f64 {
        let curr = self.current as f64;
        curr * INA_CURRENT_LSB * 1.0e3
    }

    pub fn get_power_mw(&self) -> f64 {
        let pow = self.power as f64;
        let ratio: f64 = INA_LSB_POWER as f64;
        pow * ratio * INA_CURRENT_LSB * 1.0e3
    }

    pub fn get_bus_voltage_mv(&self) -> f64 { //max: 40.96V
        self.bus_voltage as f64 * INA_LSB_BUS_VOLTAGE * 1.0e3
    }

    pub fn get_bus_voltage_v(&self) -> f64 { //max: 40.96V
        self.bus_voltage as f64 * INA_LSB_BUS_VOLTAGE
    }

    pub fn get_shunt_voltage_mv(&self) -> f64 { //max: 81.92mV
        self.shunt_voltage as f64 * INA_LSB_SHUNT_VOLTAGE * 1.0e3
    }

    pub fn print_ina(&self) -> () {
        println!("PacketIna [current: {:.2}mA, power: {:.2}mW, bus_voltage: {:.2}V, shunt_voltage: {:.2}mV]",
            self.get_current_ma(), self.get_power_mw(), self.get_bus_voltage_v(), self.get_shunt_voltage_mv())
    }
}

//ov2640
#[derive(Debug)]
struct PacketCamera {
    frame_id: u32,
    timestamp_ms: u32,
    width: u16,
    height: u16,
}

pub const MPU_SIZE: usize = 22;

//MPU9250+BMP280
#[derive(Debug)]
pub struct PacketImu { 
    accel_x: i16,
    accel_y: i16,
    accel_z: i16,
    gyro_x: i16,
    gyro_y: i16,
    gyro_z: i16,
    temperature_chip: i16,
    pressure: i32,
    temperature: i32,
}

impl PacketImu {
    pub fn get_pressure_bar(&self) -> f64 {
        let press = self.pressure as f64;
        press * 1e-5
    }

    pub fn get_temperature_deg(&self) -> f64 {
        let temp =  self.temperature as f64;
        temp / 100.0
    }

    pub fn get_temperature_chip_deg(&self) -> f64 {
        let temp = self.temperature_chip as f64;
        (temp - 0.0)/333.87 + 21.0
    }

    pub fn get_accel_g(&self) -> (f64, f64, f64) {
        let accel_x = self.accel_x as f64 / 16384.0;
        let accel_y = self.accel_y as f64 / 16384.0;
        let accel_z = self.accel_z as f64 / 16384.0;
        (accel_x, accel_y, accel_z)
    }

    pub fn get_gyro_deg_s(&self) -> (f64, f64, f64) {
        let gyro_x = self.gyro_x as f64 / 131.0;
        let gyro_y = self.gyro_y as f64 / 131.0;
        let gyro_z = self.gyro_z as f64 / 131.0;
        (gyro_x, gyro_y, gyro_z)
    }

    pub fn print_imu(&self) -> () {
        let (ax, ay, az) = self.get_accel_g();
        let (gx, gy, gz) = self.get_gyro_deg_s();
        println!("PacketImu [accel_x: {:.2}g, accel_y: {:.2}g, accel_z: {:.2}g, \
            gyro_x: {:.2}°/s, gyro_y: {:.2}°/s, gyro_z: {:.2}°/s, temperature_chip: {:.2}°C, \
            pressure: {:.3}bar, temperature: {:.2}°C]",
            ax, ay, az,
            gx, gy, gz,
            self.get_temperature_chip_deg(), self.get_pressure_bar(), self.get_temperature_deg());
    }
}

//esp32
#[derive(Debug)]
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
#[derive(Debug)]
struct PacketTemperature {
    temperature: i16,
}

pub const HC_SIZE: usize = 8;

//HC-SR04
#[derive(Debug)]
pub struct PacketUltrasonic {
    duration: i64,
}

impl PacketUltrasonic {
    pub fn get_distance_cm(&self) -> f64 {
        let dur = self.duration as f64;
        dur / 58.0
    }

    pub fn print_hc(&self) -> () {
        println!("PacketUltrasonic [distance: {:.2}cm]", self.get_distance_cm());
    }
}

//Buffer from ESP
pub struct TelemetryPacket {
    pub hall: Option<PacketHall>,
    pub ina: Option<PacketIna>,
    pub imu: Option<PacketImu>,
    pub ultrasonic: Option<PacketUltrasonic>,
}



