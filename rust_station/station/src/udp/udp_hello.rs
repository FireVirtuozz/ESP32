use std::{collections::HashMap, format, io::ErrorKind, matches, net::{SocketAddr, UdpSocket}, sync::{Arc, RwLock}, thread, time::{Duration, Instant}};

use log::{debug, error, info};

use crate::{config::AppConfig, error::AppError};

const HELLO_BEGIN: u8 = 1;
const HEARBEAT_PING_PONG: u8 = 2;
const GET_INFO: u8 = 3;

pub enum EspMsg {
    Hello(EspHello),
    Heartbeat(EspHeartbeat),
}

impl EspMsg {
    pub fn from_buffer(buf: &[u8]) -> Result<Self, AppError> {
        match buf.first() {
            Some(&HELLO_BEGIN) if buf.len() >= 8 => Ok(EspMsg::Hello(EspHello::from_buffer(&buf[1 ..])?)),
            Some(&HEARBEAT_PING_PONG) if buf.len() >= 6 => Ok(EspMsg::Heartbeat(EspHeartbeat::from_buffer(&buf[1 ..])?)),
            _ => Err(AppError::Other("Hello packet error".to_string())),
        }
    }
}

// message broadcast by ESPs when they search for the station
#[derive(Debug, Clone)]
pub struct EspHello {
    id: u8,
    mac: [u8; 6],
}

impl EspHello {
    pub fn from_buffer(buffer: &[u8]) -> Result<Self, AppError> {
        Ok(
            EspHello { 
                id: buffer[0], 
                mac: buffer[1 .. 7].try_into()?,
            }
        )
    }
}

// heartbeat pong from ESP
pub struct EspHeartbeat {
    id: u8,
    timestamp_sta: u32, // timestamp ping
}

impl EspHeartbeat {
    const SIZE: usize = 5;

    pub fn from_buffer(buffer: &[u8]) -> Result<Self, AppError> {
        Ok(EspHeartbeat {
            id: buffer[0],
            timestamp_sta: u32::from_le_bytes(buffer[1 .. 5].try_into()?)
        })
    }

    pub fn to_buffer(&self) -> [u8; Self::SIZE + 1] {
        let mut buffer = [0u8; Self::SIZE + 1];
        buffer[0] = HEARBEAT_PING_PONG;
        buffer[1] = self.id;
        buffer[2..6].copy_from_slice(&self.timestamp_sta.to_le_bytes());
        buffer
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct EspPeer {
    pub id: u8,
    pub mac: [u8; 6],
    pub ip_addr: SocketAddr,
    pub rtt_hb: u32,
    pub last_pong_hb: Instant,
    // TODO: more fields when ESP 2nd ack (see system_lib.c)
}

impl EspPeer {
    pub fn from_esp_hello(esp: EspHello, addr: SocketAddr) -> Self {
        Self {
            id: esp.id,
            ip_addr: addr,
            mac: esp.mac,
            last_pong_hb: Instant::now(),
            rtt_hb: 0,
        }
    }
}

pub fn udp_hello_server_init(
    esps_connected: Arc<RwLock<HashMap<u8, EspPeer>>>,
    config_udp_recv: AppConfig,
    start_instant: Instant,
) -> thread::JoinHandle<()> {

    let handle = thread::spawn(move || {
        if let Err(e) = udp_hello_loop(esps_connected, config_udp_recv, start_instant) {
            error!("UDP error recv: {:?}", e);
        }
    });
    handle
}

fn udp_hello_loop(
    esps_connected: Arc<RwLock<HashMap<u8, EspPeer>>>,
    config_udp_recv: AppConfig,
    start_instant: Instant,
) -> Result<(), AppError> {
    let sock = UdpSocket::bind(("0.0.0.0", config_udp_recv.udp_port_hello))?;
    sock.set_read_timeout(Some(Duration::from_millis(100)))?;
    let mut last_hb = Instant::now();
    let mut buf = [0u8; 64];

    loop {
        match sock.recv_from(&mut buf) {
            Ok((n, src)) => match EspMsg::from_buffer(&buf[..n]) {
                Ok( msg ) => match msg {
                    EspMsg::Hello (pck) => {
                        let mut esps_ref = esps_connected.write().unwrap(); 
                        let hb = EspHeartbeat {id: pck.id, timestamp_sta: start_instant.elapsed().as_millis() as u32};
                        esps_ref.insert(pck.id, EspPeer::from_esp_hello(pck, src));
                        drop(esps_ref);
                        info!("hello from {}, id {}, sending hb..", src.ip(), hb.id);
                        // send get esp more info
                        //let addr = format!("{}:{}", src.ip(), config_udp_recv.udp_port_hello); same port
                        if let Err(e) = sock.send_to(&hb.to_buffer(), src) {
                            error!("Error ({e}) sending heartbeat");
                        }
                    },
                    EspMsg::Heartbeat (pck) => {
                        let mut esps_ref = esps_connected.write().unwrap();
                        if let Some(p) = esps_ref.get_mut(&pck.id) {
                            p.rtt_hb = (start_instant.elapsed().as_millis() as u32).wrapping_sub(pck.timestamp_sta);
                            p.last_pong_hb = Instant::now();
                            debug!("hb from {}, id {}, rtt {}", src.ip(), p.id, p.rtt_hb);
                        }
                    },
                },
                Err(e) => { error!("Error ({e}) parsing hello"); },
            },
            Err(e) if matches!(e.kind(), 
                ErrorKind::WouldBlock | ErrorKind::TimedOut | ErrorKind::ConnectionReset) => {}
            Err(e) => { error!("recv_from failed on hello: {e}"); }
        }
        if last_hb.elapsed() > Duration::from_millis(200) {
            let esps_ref = esps_connected.read().unwrap();
            for p in esps_ref.values() { 
                let hb = EspHeartbeat {id: p.id, timestamp_sta: start_instant.elapsed().as_millis() as u32};
                let addr = format!("{}:{}", p.ip_addr.ip(), config_udp_recv.udp_port_link);
                if let Err(e) = sock.send_to(&hb.to_buffer(), addr) {
                    error!("Error ({e}) sending heartbeat");
                }
            }
            last_hb = Instant::now();
        }
    }
}