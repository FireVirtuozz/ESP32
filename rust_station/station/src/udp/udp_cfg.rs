use std::{
    collections::HashMap, format, io::ErrorKind, net::UdpSocket, sync::{
        Arc, RwLock, atomic::{AtomicU64, Ordering}, mpsc::{Receiver, RecvTimeoutError, Sender},
    }, thread, time::{Duration, Instant},
};
 
use log::{debug, error, warn};
 
use crate::{
    camera::{CameraConfig, LEN}, config::AppConfig, error::AppError, udp::{BUFFER_MAX_UDP_SIZE, udp_hello::EspPeer},
};
 
/// Port de config de l'ESP caméra.
const ESP_CAM_CFG_PORT: u16 = 3334;
 
/// Une commande SET sans réponse est renvoyée (elle est idempotente : valeurs absolues).
const ACK_TIMEOUT: Duration = Duration::from_millis(300);
const MAX_TRIES: u32 = 3;
 
/// Octet de type des datagrammes station -> ESP (le `switch (temp_buffer[0])` de l'ESP).
/// 1 = config générale et 3 = OTA existent aussi côté ESP : on ne les envoie jamais d'ici.
mod to_esp {
    pub const CAM_CONFIG: u8 = 2; // + charge utile de CameraConfig (masque + champs)
    pub const GET_REG: u8 = 4; //    + reg u16 | mask u8
    pub const SET_REG: u8 = 5; //    + reg u16 | mask u8 | value u8
    pub const CAM_STATE: u8 = 6; //  (rien) : demande l'état
}
 
/// Octet de type ESP -> station (`config_send_type_t` de l'ESP).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum FromEsp {
    GetReg = 1, // value i32 | reg u16 | mask u8
    SetReg = 2, // ret i32 | reg u16 | mask u8 | newer i32
    CamCfg = 3, // état de la caméra (CameraConfig::from_state)
}
 
impl TryFrom<u8> for FromEsp {
    type Error = u8;
    fn try_from(v: u8) -> Result<Self, u8> {
        match v {
            1 => Ok(FromEsp::GetReg),
            2 => Ok(FromEsp::SetReg),
            3 => Ok(FromEsp::CamCfg),
            other => Err(other),
        }
    }
}
 
pub type SharedCamCfg = Arc<RwLock<Option<CameraConfig>>>;
 
/// Commandes envoyées par l'UI au thread d'envoi.
#[derive(Debug, Clone, Copy)]
pub enum CamCmd {
    Refresh,
    Set(CameraConfig),
    GetReg { reg: u16, mask: u8 },
    SetReg { reg: u16, mask: u8, value: u8 },
}
 
/// Événements renvoyés à l'UI (l'état de la config, lui, passe par `SharedCamCfg`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CamEvent {
    /// Champs refusés par l'ESP lors du dernier SET.
    Refused(u32),
    /// Aucun accusé après `MAX_TRIES` envois.
    SetTimeout,
    RegRead { reg: u16, mask: u8, value: i32 },
    RegWrite { reg: u16, mask: u8, ret: i32, newer: i32 },
}
 
// ---------------------------------------------------------------------------
// Trames
// ---------------------------------------------------------------------------
 
fn frame_state_request() -> [u8; 1] {
    [to_esp::CAM_STATE]
}
 
fn frame_set(cfg: &CameraConfig, mask: u32) -> Vec<u8> {
    let mut v = Vec::with_capacity(1 + LEN);
    v.push(to_esp::CAM_CONFIG);
    v.extend_from_slice(&cfg.payload(mask));
    v
}
 
fn frame_get_reg(reg: u16, mask: u8) -> [u8; 4] {
    let r = reg.to_le_bytes();
    [to_esp::GET_REG, r[0], r[1], mask]
}
 
fn frame_set_reg(reg: u16, mask: u8, value: u8) -> [u8; 5] {
    let r = reg.to_le_bytes();
    [to_esp::SET_REG, r[0], r[1], mask, value]
}
 
/// `p` = octets après l'octet de type : value i32 | reg u16 | mask u8 (7 octets).
fn parse_reg_read(p: &[u8]) -> Option<CamEvent> {
    if p.len() < 7 {
        return None;
    }
    Some(CamEvent::RegRead {
        value: i32::from_le_bytes([p[0], p[1], p[2], p[3]]),
        reg: u16::from_le_bytes([p[4], p[5]]),
        mask: p[6],
    })
}
 
/// `p` = ret i32 | reg u16 | mask u8 | newer i32 (11 octets).
fn parse_reg_write(p: &[u8]) -> Option<CamEvent> {
    if p.len() < 11 {
        return None;
    }
    Some(CamEvent::RegWrite {
        ret: i32::from_le_bytes([p[0], p[1], p[2], p[3]]),
        reg: u16::from_le_bytes([p[4], p[5]]),
        mask: p[6],
        newer: i32::from_le_bytes([p[7], p[8], p[9], p[10]]),
    })
}
 
// ---------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------
 
pub fn udp_cfg_server_init(
    shared: SharedCamCfg,
    cfg: AppConfig,
    rx_cmd: Receiver<CamCmd>,
    tx_event: Sender<CamEvent>,
    shared_esps: Arc<RwLock<HashMap<u8, EspPeer>>>,
) -> Result<(), AppError> {
    let socket = UdpSocket::bind(format!("0.0.0.0:{}", cfg.udp_port_cfg_sta))?;
    start(socket, shared_esps, shared, rx_cmd, tx_event)?;
    Ok(())
}
 
fn start(
    socket: UdpSocket,
    shared_esps: Arc<RwLock<HashMap<u8, EspPeer>>>,
    shared: SharedCamCfg,
    rx_cmd: Receiver<CamCmd>,
    tx_event: Sender<CamEvent>,
) -> std::io::Result<()> {
    let tx_socket = socket.try_clone()?;
    let state_count = Arc::new(AtomicU64::new(0)); // +1 à chaque état reçu : sert d'accusé
 
    let (sh, sc, ev) = (shared.clone(), state_count.clone(), tx_event.clone());
    thread::Builder::new()
        .name("cfg-recv".into())
        .spawn(move || cfg_recv_loop(socket, sh, sc, ev))?;
    thread::Builder::new()
        .name("cfg-send".into())
        .spawn(move || cfg_send_loop(tx_socket, shared_esps, shared, rx_cmd, state_count, tx_event))?;
    Ok(())
}
 
/// Writes camera config from ESP
fn cfg_recv_loop(
    socket: UdpSocket,
    shared: SharedCamCfg,
    state_count: Arc<AtomicU64>,
    tx_event: Sender<CamEvent>,
) {
    let mut buf = [0u8; BUFFER_MAX_UDP_SIZE];
    loop {
        let (amt, src) = match socket.recv_from(&mut buf) {
            Ok(r) => r,
            // Windows : un ICMP "port unreachable" fait échouer le recv suivant. Sans gravité.
            Err(e) if e.kind() == ErrorKind::ConnectionReset => continue,
            Err(e) => {
                error!("cfg recv_from: {e}");
                thread::sleep(Duration::from_millis(50));
                continue;
            }
        };
 
        handle_datagram(&buf[..amt], &shared, &state_count, &tx_event);
    }
}
 
fn handle_datagram(
    data: &[u8],
    shared: &SharedCamCfg,
    state_count: &AtomicU64,
    tx_event: &Sender<CamEvent>,
) {
    let Some((&type_byte, payload)) = data.split_first() else {
        return; // datagramme vide
    };
 
    match FromEsp::try_from(type_byte) {
        Ok(FromEsp::CamCfg) => match CameraConfig::from_state(payload) {
            Some((cfg, refused)) => {
                if let Ok(mut g) = shared.write() {
                    *g = Some(cfg);
                }
                state_count.fetch_add(1, Ordering::Release);
                if refused != 0 {
                    let _ = tx_event.send(CamEvent::Refused(refused));
                }
            }
            None => warn!("cfg cam : {} octets de payload reçus, {} attendus", payload.len(), LEN),
        },
        Ok(FromEsp::GetReg) => match parse_reg_read(payload) {
            Some(ev) => {
                let _ = tx_event.send(ev);
            }
            None => warn!("get_reg : réponse trop courte ({} octets)", payload.len()),
        },
        Ok(FromEsp::SetReg) => match parse_reg_write(payload) {
            Some(ev) => {
                let _ = tx_event.send(ev);
            }
            None => warn!("set_reg : réponse trop courte ({} octets)", payload.len()),
        },
        Err(t) => warn!("cfg : octet de type {t} inconnu"),
    }
}
 
struct Pending {
    frame: Vec<u8>,
    sent: Instant,
    tries: u32,
    seen: u64, // valeur de state_count à l'envoi
}
 
/// Envoie les commandes de l'UI, renvoie un SET tant qu'aucun état n'est revenu,
/// et redemande l'état tant qu'on n'en a pas.
fn cfg_send_loop(
    sock: UdpSocket,
    shared_esps: Arc<RwLock<HashMap<u8, EspPeer>>>,
    shared: SharedCamCfg,
    rx: Receiver<CamCmd>,
    state_count: Arc<AtomicU64>,
    tx_event: Sender<CamEvent>,
) {
    let send = |bytes: &[u8]| {
        let esp_ref = shared_esps.read().unwrap();
        let addr = if let Some(esp) = esp_ref.get(&1) {
            Some(esp.ip_addr.ip())
        } else {
            None
        };
        drop(esp_ref);
        if let Some(ip) = addr {
            let ad = format!("{}:{}", ip, ESP_CAM_CFG_PORT);
            if let Err(e) = sock.send_to(bytes, ad) {
                error!("cfg send: {e}");
            }
        }
    };
 
    send(&frame_state_request()); // demande initiale
    let mut last_get = Instant::now();
    let mut pending: Option<Pending> = None;
 
    loop {
        match rx.recv_timeout(Duration::from_millis(100)) {
            Ok(CamCmd::Refresh) => {
                send(&frame_state_request());
                last_get = Instant::now();
            }
            Ok(CamCmd::Set(draft)) => {
                let old = shared.read().map(|g| *g).unwrap_or(None); // Copy : verrou relâché
                match old {
                    Some(old) => {
                        let mask = draft.diff(&old);
                        if mask != 0 {
                            let frame = frame_set(&draft, mask);
                            send(&frame);
                            pending = Some(Pending {
                                frame,
                                sent: Instant::now(),
                                tries: 1,
                                seen: state_count.load(Ordering::Acquire),
                            });
                        }
                    }
                    None => warn!("Set ignoré : état de l'ESP pas encore reçu"),
                }
            }
            Ok(CamCmd::GetReg { reg, mask }) => send(&frame_get_reg(reg, mask)),
            Ok(CamCmd::SetReg { reg, mask, value }) => send(&frame_set_reg(reg, mask, value)),
            Err(RecvTimeoutError::Timeout) => {}
            Err(RecvTimeoutError::Disconnected) => break, // l'UI a disparu
        }
 
        // Accusé = un nouvel état est arrivé depuis l'envoi.
        let mut clear = false;
        if let Some(p) = pending.as_mut() {
            if state_count.load(Ordering::Acquire) != p.seen {
                clear = true;
            } else if p.sent.elapsed() >= ACK_TIMEOUT {
                if p.tries >= MAX_TRIES {
                    warn!("SET caméra sans réponse après {MAX_TRIES} envois");
                    let _ = tx_event.send(CamEvent::SetTimeout);
                    send(&frame_state_request()); // resynchronise l'état affiché
                    clear = true;
                } else {
                    send(&p.frame);
                    p.tries += 1;
                    p.sent = Instant::now();
                }
            }
        }
        if clear {
            pending = None;
        }
 
        // Pas encore d'état : on redemande chaque seconde.
        let no_state = shared.read().map(|g| g.is_none()).unwrap_or(true);
        if no_state && last_get.elapsed() >= Duration::from_secs(1) {
            send(&frame_state_request());
            last_get = Instant::now();
        }
    }
}