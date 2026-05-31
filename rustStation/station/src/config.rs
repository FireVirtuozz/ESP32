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
}

impl AppConfig {
    pub fn load() -> Self {

        let filename = "config.toml";
        
        let contents = fs::read_to_string(filename)
            .unwrap_or_else(|_| panic!("config.toml {} not found!", filename));
        
        toml::from_str(&contents)
            .unwrap_or_else(|e| panic!("Syntax error in file {} : {}", filename, e))
    }
}