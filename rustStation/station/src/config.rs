use serde::Deserialize;
use std::fs;

#[derive(Deserialize, Clone, Copy, Debug, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum CamResolution {
    Qqvga, // 160x120
    Qvga,  // 320x240
    Vga,   // 640x480
}

impl Default for CamResolution {
    fn default() -> Self {
        CamResolution::Qqvga
    }
}

impl CamResolution {
    pub fn dimensions(&self) -> (usize, usize) {
        match self {
            CamResolution::Qqvga => (160, 120),
            CamResolution::Qvga  => (320, 240),
            CamResolution::Vga   => (640, 480),
        }
    }
}

#[derive(Deserialize, Clone, Copy, Debug, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum CamFormat {
    JPEG,
    YUV,
    RGB,
}

impl Default for CamFormat {
    fn default() -> Self {
        CamFormat::JPEG
    }
}

#[derive(Deserialize, Clone, Debug, Default)]
pub struct AppConfig {
    pub cam_format: CamFormat,
    pub cam_res: CamResolution,
    pub udp_port_vid: u16,
    pub debug_frag_udp_cam: bool,
    pub udp_port_ctrl: u16,
    pub udp_port_sensors: u16,
    pub udp_port_dump: u16,
    pub udp_port_logs: u16,
    pub ip_addr_esp: String,
    pub recording: bool,
    pub replay: bool,
    pub replay_file: String,
    pub replay_speed: f64,
}

impl AppConfig {
    pub fn load() -> Self {
        #[cfg(target_os = "android")]
        {
            Self {
                cam_format: CamFormat::JPEG,
                cam_res: CamResolution::Qqvga,
                debug_frag_udp_cam: false,
                ip_addr_esp: String::from("192.168.4.1"),
                udp_port_ctrl: 3333,
                udp_port_dump: 34256,
                udp_port_logs: 34257,
                udp_port_sensors: 34254,
                udp_port_vid: 34255,
                recording: false,
                replay: false,
                replay_file: String::new(),
                replay_speed: 1.0,
            }
        }

        #[cfg(not(target_os = "android"))]
        {
            let filename = "config.toml";
            
            let contents = fs::read_to_string(filename)
                .unwrap_or_else(|_| panic!("config.toml {} not found!", filename));
            
            toml::from_str(&contents)
                .unwrap_or_else(|e| panic!("Syntax error in file {} : {}", filename, e))
        }
    }
}