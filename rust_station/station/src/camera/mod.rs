use std::{
    dbg, fmt, sync::{
        Arc, RwLock, atomic::{AtomicU32, AtomicU64, Ordering}, mpsc::Receiver,
    }, thread, time::{Duration, Instant}, write,
};

use image::{load_from_memory_with_format, ImageFormat};
use log::error;
use serde::Deserialize;

// Bits du masque, dans l'ordre de l'enum CAM_CFG_* côté ESP.
pub const CAM_CFG_FRAMESIZE: u32      = 1 << 0;
pub const CAM_CFG_BRIGHTNESS: u32     = 1 << 1;
pub const CAM_CFG_CONTRAST: u32       = 1 << 2;
pub const CAM_CFG_SATURATION: u32     = 1 << 3;
pub const CAM_CFG_SHARPNESS: u32      = 1 << 4;
pub const CAM_CFG_DENOISE: u32        = 1 << 5;
pub const CAM_CFG_QUALITY: u32        = 1 << 6;
pub const CAM_CFG_GAINCEILING: u32    = 1 << 7;
pub const CAM_CFG_COLORBAR: u32       = 1 << 8;
pub const CAM_CFG_WHITEBAL: u32       = 1 << 9;
pub const CAM_CFG_AWB_GAIN: u32       = 1 << 10;
pub const CAM_CFG_WB_MODE: u32        = 1 << 11;
pub const CAM_CFG_EXPOSURE_CTRL: u32  = 1 << 12;
pub const CAM_CFG_AEC2: u32           = 1 << 13;
pub const CAM_CFG_AE_LEVEL: u32       = 1 << 14;
pub const CAM_CFG_AEC_VALUE: u32      = 1 << 15;
pub const CAM_CFG_GAIN_CTRL: u32      = 1 << 16;
pub const CAM_CFG_AGC_GAIN: u32       = 1 << 17;
pub const CAM_CFG_HMIRROR: u32        = 1 << 18;
pub const CAM_CFG_VFLIP: u32          = 1 << 19;
pub const CAM_CFG_DCW: u32            = 1 << 20;
pub const CAM_CFG_BPC: u32            = 1 << 21;
pub const CAM_CFG_WPC: u32            = 1 << 22;
pub const CAM_CFG_RAW_GMA: u32        = 1 << 23;
pub const CAM_CFG_LENC: u32           = 1 << 24;
pub const CAM_CFG_SPECIAL_EFFECT: u32 = 1 << 25;
pub const CAM_CFG_XCLK: u32           = 1 << 26;
pub const CAM_CFG_PIXFORMAT: u32      = 1 << 27;

/// Tous les bits utilisés (0..=27).
pub const ALL: u32 = (1 << 28) - 1;
/// Taille de la charge utile : masque (4) + champs (29) + pixformat (1).
pub const LEN: usize = 35;

#[derive(Deserialize, Clone, Copy, Debug, PartialEq, Default)]
#[serde(rename_all = "lowercase")]
pub enum CamResolution {
    #[default]
    Qqvga, // 160x120
    Qvga, // 320x240
    Vga,  // 640x480
}

impl CamResolution {
    pub fn dimensions(&self) -> (usize, usize) {
        match self {
            CamResolution::Qqvga => (160, 120),
            CamResolution::Qvga => (320, 240),
            CamResolution::Vga => (640, 480),
        }
    }
}

#[derive(Deserialize, Clone, Copy, Debug, PartialEq, Default)]
#[serde(rename_all = "lowercase")]
pub enum CamFormat {
    #[default]
    JPEG,
    YUV,
    RGB,
}

/// Format de pixel renvoyé par l'ESP (`pixformat_t` de esp32-camera).
/// Valeurs à vérifier dans ton sensor.h.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PixFormat {
    Rgb565,
    Yuv422,
    Grayscale,
    Jpeg,
    Other(u8),
}

impl PixFormat {
    pub fn from_u8(v: u8) -> Self {
        match v {
            0 => PixFormat::Rgb565,
            1 => PixFormat::Yuv422,
            3 => PixFormat::Grayscale,
            4 => PixFormat::Jpeg,
            other => PixFormat::Other(other),
        }
    }

    /// Octets par pixel des formats bruts que l'on sait afficher.
    pub fn bytes_per_pixel(self) -> Option<usize> {
        match self {
            PixFormat::Yuv422 => Some(2),
            PixFormat::Grayscale => Some(1),
            _ => None,
        }
    }
}

/// `framesize_t` de esp32-camera -> dimensions. À vérifier dans ton sensor.h.
pub fn framesize_dims(fs: u8) -> Option<(usize, usize)> {
    Some(match fs {
        0 => (96, 96),
        1 => (160, 120),
        2 => (128, 128),
        3 => (176, 144),
        4 => (240, 176),
        5 => (240, 240),
        6 => (320, 240),
        7 => (320, 320),
        8 => (400, 296),
        9 => (480, 320),
        10 => (640, 480),
        11 => (800, 600),
        12 => (1024, 768),
        13 => (1280, 720),
        14 => (1280, 1024),
        15 => (1600, 1200),
        _ => return None,
    })
}

#[derive(Default, Clone, Copy, Debug, PartialEq)]
pub struct CameraConfig {
    pub framesize: u8,
    pub brightness: i8,
    pub contrast: i8,
    pub saturation: i8,
    pub sharpness: i8,
    pub denoise: u8,
    pub quality: u8,
    pub gainceiling: u16,
    pub colorbar: bool,
    pub whitebal: bool,
    pub awb_gain: bool,
    pub wb_mode: u8,
    pub exposure_ctrl: bool,
    pub aec2: bool,
    pub ae_level: i8,
    pub aec_value: u16,
    pub gain_ctrl: bool,
    pub agc_gain: u8,
    pub hmirror: bool,
    pub vflip: bool,
    pub dcw: bool,
    pub bpc: bool,
    pub wpc: bool,
    pub raw_gma: bool,
    pub lenc: bool,
    pub special_effect: u8,
    pub xclk_khz: u16,
    /// Lecture seule en pratique : l'ESP refuse un changement de format à chaud.
    pub pixformat: u8,
}

impl CameraConfig {
    pub fn pix_format(&self) -> PixFormat {
        PixFormat::from_u8(self.pixformat)
    }

    /// Masque des champs de `self` qui diffèrent de `old`.
    pub fn diff(&self, old: &Self) -> u32 {
        let mut m = 0u32;
        let mut set = |bit: u32, changed: bool| {
            if changed {
                m |= bit;
            }
        };
        set(CAM_CFG_FRAMESIZE, self.framesize != old.framesize);
        set(CAM_CFG_BRIGHTNESS, self.brightness != old.brightness);
        set(CAM_CFG_CONTRAST, self.contrast != old.contrast);
        set(CAM_CFG_SATURATION, self.saturation != old.saturation);
        set(CAM_CFG_SHARPNESS, self.sharpness != old.sharpness);
        set(CAM_CFG_DENOISE, self.denoise != old.denoise);
        set(CAM_CFG_QUALITY, self.quality != old.quality);
        set(CAM_CFG_GAINCEILING, self.gainceiling != old.gainceiling);
        set(CAM_CFG_COLORBAR, self.colorbar != old.colorbar);
        set(CAM_CFG_WHITEBAL, self.whitebal != old.whitebal);
        set(CAM_CFG_AWB_GAIN, self.awb_gain != old.awb_gain);
        set(CAM_CFG_WB_MODE, self.wb_mode != old.wb_mode);
        set(CAM_CFG_EXPOSURE_CTRL, self.exposure_ctrl != old.exposure_ctrl);
        set(CAM_CFG_AEC2, self.aec2 != old.aec2);
        set(CAM_CFG_AE_LEVEL, self.ae_level != old.ae_level);
        set(CAM_CFG_AEC_VALUE, self.aec_value != old.aec_value);
        set(CAM_CFG_GAIN_CTRL, self.gain_ctrl != old.gain_ctrl);
        set(CAM_CFG_AGC_GAIN, self.agc_gain != old.agc_gain);
        set(CAM_CFG_HMIRROR, self.hmirror != old.hmirror);
        set(CAM_CFG_VFLIP, self.vflip != old.vflip);
        set(CAM_CFG_DCW, self.dcw != old.dcw);
        set(CAM_CFG_BPC, self.bpc != old.bpc);
        set(CAM_CFG_WPC, self.wpc != old.wpc);
        set(CAM_CFG_RAW_GMA, self.raw_gma != old.raw_gma);
        set(CAM_CFG_LENC, self.lenc != old.lenc);
        set(CAM_CFG_SPECIAL_EFFECT, self.special_effect != old.special_effect);
        set(CAM_CFG_XCLK, self.xclk_khz != old.xclk_khz);
        set(CAM_CFG_PIXFORMAT, self.pixformat != old.pixformat);
        m
    }

    /// Charge utile : masque (u32 LE) puis les champs. Mêmes offsets que l'ESP.
    pub fn payload(&self, mask: u32) -> [u8; LEN] {
        let mut b = [0u8; LEN];
        b[0..4].copy_from_slice(&mask.to_le_bytes());
        b[4] = self.framesize;
        b[5] = self.brightness as u8;
        b[6] = self.contrast as u8;
        b[7] = self.saturation as u8;
        b[8] = self.sharpness as u8;
        b[9] = self.denoise;
        b[10] = self.quality;
        b[11..13].copy_from_slice(&self.gainceiling.to_le_bytes());
        b[13] = self.colorbar as u8;
        b[14] = self.whitebal as u8;
        b[15] = self.awb_gain as u8;
        b[16] = self.wb_mode;
        b[17] = self.exposure_ctrl as u8;
        b[18] = self.aec2 as u8;
        b[19] = self.ae_level as u8;
        b[20..22].copy_from_slice(&self.aec_value.to_le_bytes());
        b[22] = self.gain_ctrl as u8;
        b[23] = self.agc_gain;
        b[24] = self.hmirror as u8;
        b[25] = self.vflip as u8;
        b[26] = self.dcw as u8;
        b[27] = self.bpc as u8;
        b[28] = self.wpc as u8;
        b[29] = self.raw_gma as u8;
        b[30] = self.lenc as u8;
        b[31] = self.special_effect;
        b[32..34].copy_from_slice(&self.xclk_khz.to_le_bytes());
        b[34] = self.pixformat;
        b
    }

    /// Lit une charge utile d'état. Le masque (octets 0..4) contient, dans ce sens,
    /// les champs REFUSÉS par l'ESP : voir `from_state`.
    pub fn from_payload(b: &[u8]) -> Option<Self> {
        if b.len() < LEN {
            return None;
        }
        Some(Self {
            framesize: b[4],
            brightness: b[5] as i8,
            contrast: b[6] as i8,
            saturation: b[7] as i8,
            sharpness: b[8] as i8,
            denoise: b[9],
            quality: b[10],
            gainceiling: u16::from_le_bytes([b[11], b[12]]),
            colorbar: b[13] != 0,
            whitebal: b[14] != 0,
            awb_gain: b[15] != 0,
            wb_mode: b[16],
            exposure_ctrl: b[17] != 0,
            aec2: b[18] != 0,
            ae_level: b[19] as i8,
            aec_value: u16::from_le_bytes([b[20], b[21]]),
            gain_ctrl: b[22] != 0,
            agc_gain: b[23],
            hmirror: b[24] != 0,
            vflip: b[25] != 0,
            dcw: b[26] != 0,
            bpc: b[27] != 0,
            wpc: b[28] != 0,
            raw_gma: b[29] != 0,
            lenc: b[30] != 0,
            special_effect: b[31],
            xclk_khz: u16::from_le_bytes([b[32], b[33]]),
            pixformat: b[34],
        })
    }

    /// État de l'ESP + masque des champs qu'il a refusés lors du dernier SET.
    pub fn from_state(b: &[u8]) -> Option<(Self, u32)> {
        let cfg = Self::from_payload(b)?;
        dbg!(cfg.gainceiling);
        let refused = u32::from_le_bytes([b[0], b[1], b[2], b[3]]);
        Some((cfg, refused))
    }

    /// Copie de `self` où les champs désignés par `mask` viennent de `other`.
    pub fn overlay(&self, other: &Self, mask: u32) -> Self {
        let mut o = *self;
        macro_rules! take {
            ($bit:expr, $f:ident) => {
                if mask & $bit != 0 {
                    o.$f = other.$f;
                }
            };
        }
        take!(CAM_CFG_FRAMESIZE, framesize);
        take!(CAM_CFG_BRIGHTNESS, brightness);
        take!(CAM_CFG_CONTRAST, contrast);
        take!(CAM_CFG_SATURATION, saturation);
        take!(CAM_CFG_SHARPNESS, sharpness);
        take!(CAM_CFG_DENOISE, denoise);
        take!(CAM_CFG_QUALITY, quality);
        take!(CAM_CFG_GAINCEILING, gainceiling);
        take!(CAM_CFG_COLORBAR, colorbar);
        take!(CAM_CFG_WHITEBAL, whitebal);
        take!(CAM_CFG_AWB_GAIN, awb_gain);
        take!(CAM_CFG_WB_MODE, wb_mode);
        take!(CAM_CFG_EXPOSURE_CTRL, exposure_ctrl);
        take!(CAM_CFG_AEC2, aec2);
        take!(CAM_CFG_AE_LEVEL, ae_level);
        take!(CAM_CFG_AEC_VALUE, aec_value);
        take!(CAM_CFG_GAIN_CTRL, gain_ctrl);
        take!(CAM_CFG_AGC_GAIN, agc_gain);
        take!(CAM_CFG_HMIRROR, hmirror);
        take!(CAM_CFG_VFLIP, vflip);
        take!(CAM_CFG_DCW, dcw);
        take!(CAM_CFG_BPC, bpc);
        take!(CAM_CFG_WPC, wpc);
        take!(CAM_CFG_RAW_GMA, raw_gma);
        take!(CAM_CFG_LENC, lenc);
        take!(CAM_CFG_SPECIAL_EFFECT, special_effect);
        take!(CAM_CFG_XCLK, xclk_khz);
        take!(CAM_CFG_PIXFORMAT, pixformat);
        o
    }
}

const FIELD_NAMES: [(u32, &str); 28] = [
    (CAM_CFG_FRAMESIZE, "résolution"),
    (CAM_CFG_BRIGHTNESS, "luminosité"),
    (CAM_CFG_CONTRAST, "contraste"),
    (CAM_CFG_SATURATION, "saturation"),
    (CAM_CFG_SHARPNESS, "netteté"),
    (CAM_CFG_DENOISE, "débruitage"),
    (CAM_CFG_QUALITY, "qualité JPEG"),
    (CAM_CFG_GAINCEILING, "plafond de gain"),
    (CAM_CFG_COLORBAR, "mire"),
    (CAM_CFG_WHITEBAL, "balance des blancs"),
    (CAM_CFG_AWB_GAIN, "gain AWB"),
    (CAM_CFG_WB_MODE, "mode WB"),
    (CAM_CFG_EXPOSURE_CTRL, "auto-exposition"),
    (CAM_CFG_AEC2, "AEC2"),
    (CAM_CFG_AE_LEVEL, "niveau AE"),
    (CAM_CFG_AEC_VALUE, "exposition"),
    (CAM_CFG_GAIN_CTRL, "auto-gain"),
    (CAM_CFG_AGC_GAIN, "gain"),
    (CAM_CFG_HMIRROR, "miroir H"),
    (CAM_CFG_VFLIP, "miroir V"),
    (CAM_CFG_DCW, "DCW"),
    (CAM_CFG_BPC, "BPC"),
    (CAM_CFG_WPC, "WPC"),
    (CAM_CFG_RAW_GMA, "raw GMA"),
    (CAM_CFG_LENC, "correction lentille"),
    (CAM_CFG_SPECIAL_EFFECT, "effet"),
    (CAM_CFG_XCLK, "XCLK"),
    (CAM_CFG_PIXFORMAT, "format de pixel"),
];

/// Noms lisibles des champs désignés par `mask` (pour l'IHM et les logs).
pub fn field_names(mask: u32) -> Vec<&'static str> {
    FIELD_NAMES
        .iter()
        .filter(|(bit, _)| mask & bit != 0)
        .map(|(_, name)| *name)
        .collect()
}

// ---------------------------------------------------------------------------
// Décodage
// ---------------------------------------------------------------------------

#[derive(Debug, PartialEq)]
pub enum DecodeError {
    Jpeg(String),
    UnknownFramesize(u8),
    SizeMismatch { got: usize, expected: usize, w: usize, h: usize },
    Unsupported(PixFormat),
}

impl fmt::Display for DecodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DecodeError::Jpeg(e) => write!(f, "JPEG invalide : {e}"),
            DecodeError::UnknownFramesize(fs) => write!(f, "framesize {fs} inconnue"),
            DecodeError::SizeMismatch { got, expected, w, h } => {
                write!(f, "taille {got} octets, {expected} attendus pour {w}x{h}")
            }
            DecodeError::Unsupported(p) => write!(f, "format {p:?} non géré"),
        }
    }
}

pub fn yuv422_to_rgba(yuv: &[u8], width: usize, height: usize) -> Vec<u8> {
    let mut rgba = Vec::with_capacity(width * height * 4);

    // 2 pixels par bloc de 4 octets (Y0 U Y1 V)
    for chunk in yuv.chunks_exact(4) {
        let y0 = chunk[0] as f32;
        let u = chunk[1] as f32 - 128.0;
        let y1 = chunk[2] as f32;
        let v = chunk[3] as f32 - 128.0;

        let r_offset = 1.402 * v;
        let g_offset = -0.344136 * u - 0.714136 * v;
        let b_offset = 1.772 * u;

        for y in [y0, y1] {
            rgba.push((y + r_offset).clamp(0.0, 255.0) as u8);
            rgba.push((y + g_offset).clamp(0.0, 255.0) as u8);
            rgba.push((y + b_offset).clamp(0.0, 255.0) as u8);
            rgba.push(255);
        }
    }

    rgba
}

fn decode_jpeg(data: &[u8]) -> Result<egui::ColorImage, DecodeError> {
    let img = load_from_memory_with_format(data, ImageFormat::Jpeg)
        .map_err(|e| DecodeError::Jpeg(e.to_string()))?;
    let rgba = img.to_rgba8();
    Ok(egui::ColorImage::from_rgba_unmultiplied(
        [img.width() as usize, img.height() as usize],
        rgba.as_raw(),
    ))
}

/// Format à utiliser : celui annoncé par l'ESP. Tant qu'on n'a pas reçu sa config,
/// on se rabat sur la signature JPEG (FF D8) pour afficher quand même quelque chose.
fn resolve_format(data: &[u8], cfg: Option<CameraConfig>) -> PixFormat {
    match cfg {
        Some(c) => c.pix_format(),
        None if data.starts_with(&[0xFF, 0xD8]) => PixFormat::Jpeg,
        None => PixFormat::Other(255),
    }
}

/// Transforme une frame brute en image, selon le format et la résolution de la config.
pub fn decode_frame(data: &[u8], cfg: Option<CameraConfig>) -> Result<egui::ColorImage, DecodeError> {
    match resolve_format(data, cfg) {
        PixFormat::Jpeg => decode_jpeg(data),
        fmt @ (PixFormat::Yuv422 | PixFormat::Grayscale) => {
            let cfg = cfg.ok_or(DecodeError::Unsupported(fmt))?;
            let (w, h) = framesize_dims(cfg.framesize).ok_or(DecodeError::UnknownFramesize(cfg.framesize))?;
            let bpp = fmt.bytes_per_pixel().ok_or(DecodeError::Unsupported(fmt))?;
            let expected = w * h * bpp;
            if data.len() != expected {
                // Normal juste après un changement de résolution : frames en vol de l'ancienne taille.
                return Err(DecodeError::SizeMismatch { got: data.len(), expected, w, h });
            }
            Ok(match fmt {
                PixFormat::Grayscale => egui::ColorImage::from_gray([w, h], data),
                _ => egui::ColorImage::from_rgba_unmultiplied([w, h], &yuv422_to_rgba(data, w, h)),
            })
        }
        other => Err(DecodeError::Unsupported(other)),
    }
}

/// Évite d'inonder les logs à 26 fps : une ligne par seconde, avec le nombre d'erreurs masquées.
struct ErrorLimiter {
    last: Instant,
    suppressed: u32,
}

impl ErrorLimiter {
    fn new() -> Self {
        let last = Instant::now()
            .checked_sub(Duration::from_secs(1))
            .unwrap_or_else(Instant::now);
        Self { last, suppressed: 0 }
    }

    fn log(&mut self, e: &DecodeError) {
        if self.last.elapsed() >= Duration::from_secs(1) {
            if self.suppressed > 0 {
                error!("décodage caméra : {e} (+{} similaires)", self.suppressed);
            } else {
                error!("décodage caméra : {e}");
            }
            self.suppressed = 0;
            self.last = Instant::now();
        } else {
            self.suppressed += 1;
        }
    }
}

/// Décode les frames reçues. Ce thread ne fait ni réseau ni écriture de config :
/// il lit `shared_cfg` (format de pixel, résolution) et produit l'image à afficher.
pub fn camera_decode_thread(
    rx_img: Receiver<Vec<u8>>,
    shared_texture: Arc<RwLock<Option<egui::ColorImage>>>,
    shared_fps: Arc<AtomicU32>, // fps * 10
    shared_frame_count: Arc<AtomicU64>,
    shared_cfg: Arc<RwLock<Option<CameraConfig>>>,
) -> thread::JoinHandle<()> {
    thread::spawn(move || {
        let mut frames_in = 0u32;
        let mut last_fps_time = Instant::now();
        let mut limiter = ErrorLimiter::new();

        loop {
            // Bloque jusqu'à une frame : pas de boucle à vide.
            let mut data = match rx_img.recv() {
                Ok(d) => d,
                Err(_) => {
                    error!("rx_img closed, stopping decode thread");
                    return;
                }
            };
            // Ne garde que la plus récente (latence minimale).
            // Si tu enregistres le flux via ce même canal, ne jette rien ici.
            let mut received = 1u32;
            while let Ok(newer) = rx_img.try_recv() {
                data = newer;
                received += 1;
            }

            // Copie (CameraConfig est Copy) : le verrou est relâché tout de suite,
            // et un verrou empoisonné ne fait pas paniquer ce thread.
            let cfg: Option<CameraConfig> = shared_cfg.read().map(|g| *g).unwrap_or(None);

            match decode_frame(&data, cfg) {
                Ok(img) => {
                    if let Ok(mut tex) = shared_texture.write() {
                        *tex = Some(img);
                    }
                    shared_frame_count.fetch_add(1, Ordering::Relaxed);
                }
                Err(e) => limiter.log(&e),
            }

            frames_in += received; // fps = frames reçues, pas seulement décodées
            let elapsed = last_fps_time.elapsed();
            if elapsed.as_secs_f32() >= 1.0 {
                let fps = (frames_in as f32 / elapsed.as_secs_f32() * 10.0) as u32;
                shared_fps.store(fps, Ordering::Relaxed);
                frames_in = 0;
                last_fps_time = Instant::now();
            }
        }
    })
}
