use std::{error::Error, f64::consts::PI, io, string::FromUtf8Error};

use log::error;
use serde::{Deserialize, Serialize};

use crate::{error::AppError, sensors::parser::SensorsUdpHeader};

pub mod parser;

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
    
    Max       = 29,
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
            29 => Ok(SensorType::Max),
            _ => Err("Sensor code not valid"),
        }
    }
}

const TIRE_DIAMETER : u8 = 65; //mm

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

pub const MPU_SIZE: usize = 22;

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
    pub motor : i8,
}

impl PacketKy033 {
    pub fn get_speed_m_s(&self) -> f64 {
        let dt = 0.1; //100ms period
        let diam = TIRE_DIAMETER as f64;
        let nb_rounds = self.pulses as f64 / 1.0;
        nb_rounds / dt * PI * diam * 1.0e-3 //1 revolution per dt
    }

    pub fn get_speed_km_h(&self) -> f64 {
        let speed_m_s = self.get_speed_m_s(); 
        speed_m_s * 3.6
    }

    pub fn get_distance_m(&self) -> f64 {
        let diam = TIRE_DIAMETER as f64;
        let nb_rounds = self.pulses as f64 / 1.0;
        nb_rounds * PI * diam * 1.0e-3
    }

}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PacketPong {
    pub ping_pong : u32,
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
    /// Convertit le u8 reçu de l'ESP32 en Enum Rust sécurisé
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
            _ => Self::Unknown, // Fallback si valeur inconnue
        }
    }

    /// Renvoie le texte propre pour ton HUD egui
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Unknown => "Inconnu",
            Self::PowerOn => "Démarrage Électrique (Power On)",
            Self::ExternalPin => "Bouton Reset / Pin Externe",
            Self::Software => "Redémarrage Logiciel (esp_restart)",
            Self::Panic => "Crash Logiciel (Panic/Exception)",
            Self::InterruptWdt => "Watchdog Interruption (Bloqué)",
            Self::TaskWdt => "Watchdog Tâche (Boucle infinie)",
            Self::OtherWdt => "Watchdog Système",
            Self::DeepSleep => "Sortie de Veille (Deep Sleep)",
            Self::Brownout => "Chute de Tension (Brownout)",
            Self::Sdio => "Reset via SDIO",
            Self::Usb => "Reset via USB",
            Self::Jtag => "Reset via JTAG",
            Self::Efuse => "Erreur eFuse",
            Self::PowerGlitch => "Micro-coupure Électrique (Glitch)",
            Self::CpuLockup => "CPU Bloqué (Double Exception)",
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
    /// Convertit le u8 reçu/envoyé de l'ESP32 en Enum Rust sécurisé
    pub fn from_u8(val: u8) -> Self {
        match val {
            0 => Self::Default,
            1 => Self::Middle,
            2 => Self::Advanced,
            3 => Self::Expert,
            _ => Self::Default, // Fallback sécurisé en mode Éco si valeur corrompue
        }
    }

    /// Renvoie le texte propre pour ton HUD egui
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Default => "ECO (20%)",
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
    pub angle: u8,
    pub motor: i8,
    pub nb_packets: u32,
    pub core0: f32,
    pub core1: f32,
    pub drive_mode: DriveMode,
}



