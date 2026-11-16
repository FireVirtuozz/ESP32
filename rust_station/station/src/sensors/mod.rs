use std::{error::Error, f64::consts::PI, io, println, string::FromUtf8Error};

use log::error;
use serde::{Deserialize, Serialize};

use crate::{error::AppError, gui::screens::tuning::CurveType, sensors::{lidar::{LidarD500Batch, LidarD500Packet}, parser::SensorsUdpHeader}};

pub mod parser;
pub mod lidar;

pub const RAD_TO_DEG : f64 = 180.0 / PI;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
#[repr(u8)]
pub enum SensorType {
    Unknown   = 0,
    Hcsr04    = 1,
    Ina226    = 2,
    Ky003     = 3,
    Mpu9250   = 4,
    RfidRc522 = 5,
    Rcwl0515  = 6,
    Vl53l1x   = 7,
    As5600    = 8,
    Ky035     = 9,
    Fc33      = 10,
    Ky033     = 11,
    Ky002     = 12,
    Ky040     = 13,
    Dht11     = 14,
    Ky020     = 15,
    Ky018     = 16,
    Ky031     = 17,
    Ky017     = 18,
    Ky005     = 19,
    Ky022     = 20,
    Ky021     = 21,
    Ky004     = 22,
    Ky039     = 23,
    Ky032     = 24,
    Ky023Xy   = 25,
    Ky023Sw   = 26, 
    Esp       = 27, 
    Pong      = 28,
    Motor     = 29,
    Brake     = 30,
    Bmp280    = 31,
    Ds18b20   = 32,
    Bno085Rot     = 33,
    Bno085Accel    = 34,
    Bno085Stability = 35,
    Bno085LinearAccel = 36,
    LidarD500 = 37,
    BatAdc = 38,
    Acs758 = 39,
    Bz181 = 40,

    Max       = 41,
}

impl TryFrom<u8> for SensorType {
    type Error = &'static str;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(SensorType::Unknown),
            1 => Ok(SensorType::Hcsr04),
            2 => Ok(SensorType::Ina226),
            3 => Ok(SensorType::Ky003),
            4 => Ok(SensorType::Mpu9250),
            5 => Ok(SensorType::RfidRc522),
            6 => Ok(SensorType::Rcwl0515),
            7 => Ok(SensorType::Vl53l1x),
            8 => Ok(SensorType::As5600),
            9 => Ok(SensorType::Ky035),
            10 => Ok(SensorType::Fc33),
            11 => Ok(SensorType::Ky033),
            12 => Ok(SensorType::Ky002),
            13 => Ok(SensorType::Ky040),
            14 => Ok(SensorType::Dht11),
            15 => Ok(SensorType::Ky020),
            16 => Ok(SensorType::Ky018),
            17 => Ok(SensorType::Ky031),
            18 => Ok(SensorType::Ky017),
            19 => Ok(SensorType::Ky005),
            20 => Ok(SensorType::Ky022),
            21 => Ok(SensorType::Ky021),
            22 => Ok(SensorType::Ky004),
            23 => Ok(SensorType::Ky039),
            24 => Ok(SensorType::Ky032),
            25 => Ok(SensorType::Ky023Xy),
            26 => Ok(SensorType::Ky023Sw),
            27 => Ok(SensorType::Esp),
            28 => Ok(SensorType::Pong),
            29 => Ok(SensorType::Motor),
            30 => Ok(SensorType::Brake),
            31 => Ok(SensorType::Bmp280),
            32 => Ok(SensorType::Ds18b20),
            33 => Ok(SensorType::Bno085Rot),
            34 => Ok(SensorType::Bno085Accel),
            35 => Ok(SensorType::Bno085Stability),
            36 => Ok(SensorType::Bno085LinearAccel),
            37 => Ok(SensorType::LidarD500),
            38 => Ok(SensorType::BatAdc),
            39 => Ok(SensorType::Acs758),
            40 => Ok(SensorType::Bz181),
            41 => Ok(SensorType::Max),
            _ => Err("Sensor code not valid"),
        }
    }
}

const TIRE_DIAMETER : u8 = 65; //mm
const COUNT_PULSES_PER_REV : u8 = 12;

const INA_SHUNT_RESISTANCE: f64 = 0.1; //ohms on INA226
//on datasheet, shunt voltage input range [-81.92 .. 81.92]mV
const INA_MAX_CURRENT: f64 = (81.92 * 1.0e-3) / INA_SHUNT_RESISTANCE; //~800mA
const INA_REGISTER_SIZE: f64 = 32768.0; //2^15, 1 register on 16 bits (1 sign bit)
const INA_CURRENT_LSB: f64 = INA_MAX_CURRENT / INA_REGISTER_SIZE;
const INA_LSB_POWER: u8 = 25;
const INA_LSB_SHUNT_VOLTAGE: f64 = 2.5 * 1.0e-6; //2.5uV
const INA_LSB_BUS_VOLTAGE: f64 = 1.25 * 1.0e-3; //1.25mV

pub const KY_SIZE: usize = 16;

//KY-003
#[derive(Debug, Serialize, Deserialize, Clone)]
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
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketIna {
    pub current: i16,
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

pub const BMP_SIZE: usize = 8;

//BMP280
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketBmp { 
    pressure: i32,
    temperature: i32,
} 

impl PacketBmp {
    pub fn get_pressure_bar(&self) -> f64 {
        let press = self.pressure as f64;
        press * 1e-5
    }

    pub fn get_temperature_deg(&self) -> f64 {
        let temp =  self.temperature as f64;
        temp / 100.0
    }
}

pub const MPU_SIZE: usize = 14;

//MPU9250+BMP280
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketImu { 
    accel_x: i16,
    accel_y: i16,
    accel_z: i16,
    gyro_x: i16,
    gyro_y: i16,
    gyro_z: i16,
    temperature_chip: i16,
}

impl PacketImu {

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
            gyro_x: {:.2}°/s, gyro_y: {:.2}°/s, gyro_z: {:.2}°/s, temperature_chip: {:.2}°C",
            ax, ay, az,
            gx, gy, gz,
            self.get_temperature_chip_deg());
    }
}

//DS18B20
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketTemperature {
    temperature: i16,
}

pub const HC_SIZE: usize = 8;

//HC-SR04
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketUltrasonic {
    pub hc_id: u8,
    duration: i64,
    pub blocked: bool,
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

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketRfidRc522 {
    pub uid: Vec<u8>,
}

impl PacketRfidRc522 {
    pub fn get_uid_string(&self) -> String {
        self.uid.iter()
        .map(|byte| format!("{:02X}", byte)) 
        .collect::<Vec<String>>()
        .join(":")
    }
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketRcwl0515 {
    pub detection : bool,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketKy033 {
    //careful: if nb pulses > 255 (255/100ms) -> go with u16
    pub pulses : u8, //number of pulses during 100ms (ky033 task period in esp32)
    pub motor : i16,
    pub sign_motor_positive : bool,
}

impl PacketKy033 {
    pub fn get_speed_m_s(&self) -> f64 {
        let dt = 0.02; //20ms period
        let diam = TIRE_DIAMETER as f64;
        let nb_rounds = self.pulses as f64 / COUNT_PULSES_PER_REV as f64;
        //println!("{}", self.pulses);
        nb_rounds / dt * PI * diam * 1.0e-3 //1 revolution per dt
    }

    pub fn get_speed_km_h(&self) -> f64 {
        let speed_m_s = self.get_speed_m_s(); 
        speed_m_s * 3.6
    }

    pub fn get_distance_m(&self) -> f64 {
        let diam = TIRE_DIAMETER as f64;
        let nb_rounds = self.pulses as f64 / COUNT_PULSES_PER_REV as f64;
        nb_rounds * PI * diam * 1.0e-3
    }

}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketPong {
    pub ping_pong : u32,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketMotor {
    pub curve_type: CurveType,
    pub accel_param: u8,
    pub decel_param: u8,
    pub current_motor: i16,
    pub target_motor: i16,
    pub hc_block_activated: bool,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub enum TelemetryEnum {
    HCSR04(PacketUltrasonic),
    ESP(EspPacket),
    MPU(PacketImu),
    KY003(PacketHall),
    INA226(PacketIna),
    TEMPERATURE(PacketTemperature),
    RFIDRC522(PacketRfidRc522),
    RCWL0515(PacketRcwl0515),
    KY033(PacketKy033),
    PONG(PacketPong),
    MOTOR(PacketMotor),
    BRAKE(BrakePacket),
    BMP(PacketBmp),
    DHT11(PacketDht11),
    PHOTOSENSOR(PacketPhotosensor),
    VL53L1X(PacketVl53l1x),
    BNO085ACCEL(PacketBno085Accel),
    BNO085LINEARACCEL(PacketBno085LinearAccel),
    BNO085ROTATION(PacketBno085Rotation),
    BNO085CLASSIF(PacketBno085Classif),
    LIDARD500(LidarD500Batch),
    BATADC(PacketBatAdc),
    ACS758(PacketAcs758),
    BZ181(PacketBz181),
}

//Buffer from ESP
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct TelemetryPacket {
    pub hd_info: SensorsUdpHeader,
    pub packet: TelemetryEnum,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
#[repr(u8)]
pub enum EspResetReason {
    #[default]
    Unknown = 0,
    PowerOn = 1,
    ExternalPin = 2,
    Software = 3,
    Panic = 4,
    InterruptWdt = 5,
    TaskWdt = 6,
    OtherWdt = 7,
    DeepSleep = 8,
    Brownout = 9,
    Sdio = 10,
    Usb = 11,
    Jtag = 12,
    Efuse = 13,
    PowerGlitch = 14,
    CpuLockup = 15,
}

impl EspResetReason {
    pub fn from_u8(val: u8) -> Self {
        match val {
            1 => Self::PowerOn,
            2 => Self::ExternalPin,
            3 => Self::Software,
            4 => Self::Panic,
            5 => Self::InterruptWdt,
            6 => Self::TaskWdt,
            7 => Self::OtherWdt,
            8 => Self::DeepSleep,
            9 => Self::Brownout,
            10 => Self::Sdio,
            11 => Self::Usb,
            12 => Self::Jtag,
            13 => Self::Efuse,
            14 => Self::PowerGlitch,
            15 => Self::CpuLockup,
            _ => Self::Unknown,
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Unknown => "Unknown",
            Self::PowerOn => "Power on",
            Self::ExternalPin => "Reset button",
            Self::Software => "esp_restart",
            Self::Panic => "Panic",
            Self::InterruptWdt => "Interruption watchdog (deadlock)",
            Self::TaskWdt => "Task watchdog (infinite loop)",
            Self::OtherWdt => "System watchdog",
            Self::DeepSleep => "Deep Sleep",
            Self::Brownout => "Brownout",
            Self::Sdio => "SDIO Reset",
            Self::Usb => "USB Reset",
            Self::Jtag => "JTAG Reset",
            Self::Efuse => "eFuse error",
            Self::PowerGlitch => "Electrical glitch",
            Self::CpuLockup => "CPU locked",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
#[repr(u8)]
pub enum DriveMode {
    #[default]
    Default = 0,
    Middle = 1,
    Advanced = 2,
    Expert = 3,
}

impl DriveMode {
    pub fn from_u8(val: u8) -> Self {
        match val {
            0 => Self::Default,
            1 => Self::Middle,
            2 => Self::Advanced,
            3 => Self::Expert,
            _ => Self::Default,
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Default => "ECO (25%)",
            Self::Middle => "NORMAL (40%)",
            Self::Advanced => "SPORT (70%)",
            Self::Expert => "EXPERT (100%)",
        }
    }
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct EspPacket {
    pub esp_deg: f32,
    pub rssi: i8,
    pub reset_reason: EspResetReason,
    pub free_dram: u32,
    pub free_dram_block: u32,
    pub free_psram: u32,
    pub free_psram_block: u32,
    pub angle: i16,
    pub motor: i16,
    pub nb_packets: u32,
    pub core0: f32,
    pub core1: f32,
    pub udp_drop: u32,
    pub ip_drop: u32,
    pub udp_chkerr: u32,  
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct BrakePacket {
    pub braking: bool,
    pub timeout_braking: u32,
    pub pulses_100ms: u16,
    pub pulses_20ms: u16,
    pub current_motor: i16,
}


#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct PacketPhotosensor {
    pub raw_value: i32,
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct PacketDht11 {
    pub humidity: u8,
    pub temperature: u8,
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct PacketVl53l1x {
    pub distance: u16,
    pub ambient: u16,
    pub sig_per_spad: u16,
    pub num_spad: u16,
    pub status: u8,
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub enum Bno085Status {
    #[default]
    Unreliable = 0,
    AccuracyLow = 1,
    AccuracyMedium = 2,
    AccuracyHigh = 3,
}

impl Bno085Status {
    pub fn from_u8(val: u8) -> Self {
        match val {
            0 => Self::Unreliable,
            1 => Self::AccuracyLow,
            2 => Self::AccuracyMedium,
            3 => Self::AccuracyHigh,
            _ => Self::Unreliable,
        }
    }

    pub fn to_str(self) -> String {
        match self {
            Bno085Status::Unreliable => "Unreliable".to_string(),
            Bno085Status::AccuracyLow => "Low".to_string(),
            Bno085Status::AccuracyMedium => "Medium".to_string(),
            Bno085Status::AccuracyHigh => "High".to_string(),
        }
    }
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct PacketBno085Accel {
    pub status: Bno085Status,
    pub timestamp: u32,
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl PacketBno085Accel {
    pub fn get_accel_g(&self) -> (f64, f64, f64) {
        (self.x as f64/ 9.81, self.y as f64 / 9.81, self.z as f64 / 9.81)
    }

    pub fn get_time_seconds(&self) -> f64 {
        self.timestamp as f64 * 1e-6
    }
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct PacketBno085LinearAccel {
    pub status: Bno085Status,
    pub timestamp: u32,
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl PacketBno085LinearAccel {
    pub fn get_accel_g(&self) -> (f64, f64, f64) {
        (self.x as f64/ 9.81, self.y as f64 / 9.81, self.z as f64 / 9.81)
    }

    pub fn get_time_seconds(&self) -> f64 {
        self.timestamp as f64 * 1e-6
    }
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub enum Bno085Classif {
    #[default]
    Unknown = 0,
    OnTable = 1,
    Stationary = 2,
    Stable = 3,
    Motion = 4,
}

impl Bno085Classif {
    pub fn from_u8(val: u8) -> Self {
        match val {
            0 => Self::Unknown,
            1 => Self::OnTable,
            2 => Self::Stationary,
            3 => Self::Stable,
            4 => Self::Motion,
            _ => Self::Unknown,
        }
    }

    pub fn to_str(&self) -> String {
        match self {
            Bno085Classif::Unknown => "Unknown".to_string(),
            Bno085Classif::Motion => "Motion".to_string(),
            Bno085Classif::OnTable => "On Table".to_string(),
            Bno085Classif::Stable => "Stable".to_string(),
            Bno085Classif::Stationary => "Stationary".to_string(),
        }
    }
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct PacketBno085Classif {
    pub status: Bno085Status,
    pub classif: Bno085Classif,
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct PacketBno085Rotation {
    pub status: Bno085Status,
    pub accuracy: f32,
    pub roll: f32,
    pub pitch: f32,
    pub yaw: f32,
}

impl PacketBno085Rotation {
    pub fn get_angles_deg(&self) -> (f64, f64, f64) {
        (self.roll as f64 * RAD_TO_DEG, self.pitch as f64 * RAD_TO_DEG, self.yaw as f64 * RAD_TO_DEG)
    }

    pub fn get_angles_rad(&self) -> (f32, f32, f32) {
        (self.roll, self.pitch, self.yaw)
    }

    pub fn get_accuracy_deg(&self) -> f64 {
        self.accuracy as f64 * RAD_TO_DEG
    }
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct PacketBatAdc {
    pub adc_voltage: f32,
}

impl PacketBatAdc {
    
    pub fn from_bytes(buf: &[u8]) -> Result<Self, AppError> {
        Ok(Self {
            adc_voltage: f32::from_le_bytes(buf[0 .. 4].try_into()?),
        })
    }
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct PacketAcs758 {
    pub adc_current: f32,
}

impl PacketAcs758 {
    
    pub fn from_bytes(buf: &[u8]) -> Result<Self, AppError> {
        Ok(Self {
            adc_current: f32::from_le_bytes(buf[0 .. 4].try_into()?),
        })
    }
}


#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SensorKey {
    HCSR04_0,
    HCSR04_1,
    ESP,
    MPU,
    KY003,
    INA226,
    Temperature,
    RfidRc522,
    Rcwl0515,
    Ky033,
    Pong,
    Motor,
    Brake,
    BMP,
    DHT11,
    Photosensor,
    VL53L1X,
    Bno085Accel,
    Bno085LinearAccel,
    Bno085Rotation,
    Bno085Classif,
    LidarD500Raw,
    BatAdc,
    ACS758,
    BZ181,
    Last
}

impl From<&TelemetryEnum> for SensorKey {
    fn from(packet: &TelemetryEnum) -> Self {
        match packet {
            TelemetryEnum::HCSR04(packet) => {
                if packet.hc_id == 0 {
                    SensorKey::HCSR04_0
                } else {
                    SensorKey::HCSR04_1
                }
            },
            TelemetryEnum::ESP(_) => SensorKey::ESP,
            TelemetryEnum::MPU(_) => SensorKey::MPU,
            TelemetryEnum::KY003(_) => SensorKey::KY003,
            TelemetryEnum::INA226(_) => SensorKey::INA226,
            TelemetryEnum::TEMPERATURE(_) => SensorKey::Temperature,
            TelemetryEnum::RFIDRC522(_) => SensorKey::RfidRc522,
            TelemetryEnum::RCWL0515(_) => SensorKey::Rcwl0515,
            TelemetryEnum::KY033(_) => SensorKey::Ky033,
            TelemetryEnum::PONG(_) => SensorKey::Pong,
            TelemetryEnum::MOTOR(_) => SensorKey::Motor,
            TelemetryEnum::BRAKE(_) => SensorKey::Brake,
            TelemetryEnum::BMP(_) => SensorKey::BMP,
            TelemetryEnum::DHT11(_) => SensorKey::DHT11,
            TelemetryEnum::PHOTOSENSOR(_) => SensorKey::Photosensor,
            TelemetryEnum::VL53L1X(_) => SensorKey::VL53L1X,
            TelemetryEnum::BNO085ACCEL(_) => SensorKey::Bno085Accel,
            TelemetryEnum::BNO085LINEARACCEL(_) => SensorKey::Bno085LinearAccel,
            TelemetryEnum::BNO085ROTATION(_) => SensorKey::Bno085Rotation,
            TelemetryEnum::BNO085CLASSIF(_) => SensorKey::Bno085Classif,
            TelemetryEnum::LIDARD500(_) => SensorKey::LidarD500Raw,
            TelemetryEnum::BATADC(_) => SensorKey::BatAdc,
            TelemetryEnum::ACS758(_) => SensorKey::ACS758,
            TelemetryEnum::BZ181(_) => SensorKey::BZ181,
        }
    }
}

impl SensorKey {
    pub const COUNT: usize = SensorKey::Last as usize;

    pub fn index(self) -> usize {
        self as usize
    }
}

pub const MAX_ACTIVE_SATS: usize = 16;
pub const MAX_SATELLITES_VIEW: usize = 48;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(u8)]
pub enum GnssConstellation {
    Unknown = 0,
    Gps = 1,
    Glonass = 2,
    Galileo = 3,
    Beidou = 4,
    Qzss = 5,
}

impl GnssConstellation {
    pub fn from_u8(val: u8) -> Self {
        match val {
            1 => Self::Gps,
            2 => Self::Glonass,
            3 => Self::Galileo,
            4 => Self::Beidou,
            5 => Self::Qzss,
            _ => Self::Unknown,
        }
    }

    pub fn to_str(&self) -> &str {
        match self {
            GnssConstellation::Gps => "GPS",
            GnssConstellation::Glonass => "GLONASS",
            GnssConstellation::Galileo => "Galileo",
            GnssConstellation::Beidou => "Beidou",
            GnssConstellation::Qzss => "QZSS",
            _ => "Unknown",
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Clone, Copy)]
pub struct GpsSatellite {
    pub azimuth: u16,
    pub prn: u8,
    pub elevation: u8,
    pub snr: u8,
    pub constellation: GnssConstellation,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketBz181 {
    pub utc_time: String,   // depuis [u8; 12], tronqué au premier \0
    pub utc_date: String,   // depuis [u8; 8]
    pub status: u8,         // 'A' ou 'V' en ASCII, garde brut
    pub mode_indicator: u8, // 'A'/'D'/'E'/'M'/'S'/'N', ou 0 si absent
    pub latitude: f64,
    pub longitude: f64,
    pub speed_knots: f32,
    pub speed_kmh: f32,
    pub course_over_ground: f32,

    pub fix_quality: u8,
    pub satellites_used: u8,
    pub hdop: f32,
    pub altitude_m: f32,
    pub geoid_height_m: f32,

    pub fix_mode: u8,
    pub fix_type: u8,
    pub pdop: f32,
    pub vdop: f32,
    pub active_sats_ids: [u8; MAX_ACTIVE_SATS],
    pub active_sats_count: u8,

    #[serde(with = "serde_arrays")]
    pub sats_in_view: [GpsSatellite; MAX_SATELLITES_VIEW],
    pub total_sats_in_view: u16,

    pub last_rmc_ms: u32,
    pub last_gga_ms: u32,
    pub last_gsa_ms: u32,
    pub last_gsv_ms: u32,
}

impl PacketBz181 {
    pub fn from_bytes(buf: &[u8]) -> Result<Self, AppError> {
        let mut pos = 0usize;

        // --- utc_time[12] ---
        let utc_time_raw = &buf[pos..pos + 12];
        let utc_time = String::from_utf8_lossy(utc_time_raw)
            .trim_end_matches('\0')
            .to_string();
        pos += 12;

        // --- utc_date[8] ---
        let utc_date_raw = &buf[pos..pos + 8];
        let utc_date = String::from_utf8_lossy(utc_date_raw)
            .trim_end_matches('\0')
            .to_string();
        pos += 8;

        let status = buf[pos]; pos += 1;
        let mode_indicator = buf[pos]; pos += 1;

        let latitude = f64::from_le_bytes(buf[pos..pos + 8].try_into().unwrap()); pos += 8;
        let longitude = f64::from_le_bytes(buf[pos..pos + 8].try_into().unwrap()); pos += 8;

        let speed_knots = f32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap()); pos += 4;
        let speed_kmh = f32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap()); pos += 4;
        let course_over_ground = f32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap()); pos += 4;

        let fix_quality = buf[pos]; pos += 1;
        let satellites_used = buf[pos]; pos += 1;

        let hdop = f32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap()); pos += 4;
        let altitude_m = f32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap()); pos += 4;
        let geoid_height_m = f32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap()); pos += 4;

        let fix_mode = buf[pos]; pos += 1;
        let fix_type = buf[pos]; pos += 1;

        let pdop = f32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap()); pos += 4;
        let vdop = f32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap()); pos += 4;

        let mut active_sats_ids = [0u8; MAX_ACTIVE_SATS];
        active_sats_ids.copy_from_slice(&buf[pos..pos + MAX_ACTIVE_SATS]);
        pos += MAX_ACTIVE_SATS;

        let active_sats_count = buf[pos]; pos += 1;

        let mut sats_in_view = [GpsSatellite {
            azimuth: 0, prn: 0, elevation: 0, snr: 0,
            constellation: GnssConstellation::Unknown,
        }; MAX_SATELLITES_VIEW];

        for sat in sats_in_view.iter_mut() {
            let azimuth = u16::from_le_bytes(buf[pos..pos + 2].try_into().unwrap());
            pos += 2;
            let prn = buf[pos]; pos += 1;
            let elevation = buf[pos]; pos += 1;
            let snr = buf[pos]; pos += 1;
            let constellation = GnssConstellation::from_u8(buf[pos]); pos += 1;

            *sat = GpsSatellite { azimuth, prn, elevation, snr, constellation };
        }

        let total_sats_in_view = u16::from_le_bytes(buf[pos..pos + 2].try_into().unwrap()); pos += 2;

        let last_rmc_ms = u32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap()); pos += 4;
        let last_gga_ms = u32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap()); pos += 4;
        let last_gsa_ms = u32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap()); pos += 4;
        let last_gsv_ms = u32::from_le_bytes(buf[pos..pos + 4].try_into().unwrap());
        // pos += 4; -- dernière lecture, pas besoin d'avancer pos davantage

        Ok(Self {
            utc_time, utc_date, status, mode_indicator,
            latitude, longitude, speed_knots, speed_kmh, course_over_ground,
            fix_quality, satellites_used, hdop, altitude_m, geoid_height_m,
            fix_mode, fix_type, pdop, vdop, active_sats_ids, active_sats_count,
            sats_in_view, total_sats_in_view,
            last_rmc_ms, last_gga_ms, last_gsa_ms, last_gsv_ms,
        })
    }
}