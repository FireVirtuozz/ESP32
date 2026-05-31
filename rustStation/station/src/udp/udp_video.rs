use std::{collections::HashMap, net::UdpSocket, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Sender}, thread};

use log::{debug, error};

use crate::{config::AppConfig, error::AppError, udp::{BUFFER_MAX_UDP_SIZE, FragmentReassembler, ReassemblyMode}};

pub const HEADER_VID_SIZE: usize = 4 + 1 + 1 + 1;
pub struct HeaderUdpVid {
    pub frame_id: u32,
    pub frag_total: u8,
    pub frag_idx: u8,
    pub esp_id: u8,
}

impl HeaderUdpVid {
    pub fn header_vid_parse(buf: &[u8]) -> Result<Self, AppError> {
        if buf.len() < HEADER_VID_SIZE {
            return Err("Header not valid".into())
        }

        Ok(Self {
            frame_id:    u32::from_be_bytes([buf[0], buf[1], buf[2], buf[3]]),
            frag_total:  buf[4],
            frag_idx:    buf[5],
            esp_id:      buf[6],
        })
    }
}

pub fn udp_server_video_init(
    tx_img: Sender<Vec<u8>>,
    camera_connected: Arc<AtomicBool>,
    config_udp_vid: AppConfig,
) -> thread::JoinHandle<()> {

    let handle = thread::spawn(move || {
        if let Err(e) = udp_video_loop(tx_img, camera_connected, config_udp_vid) {
            error!("UDP error vid: {:?}", e);
        }
    });
    handle
}



fn udp_video_loop(
    tx_img: Sender<Vec<u8>>,
    camera_connected: Arc<AtomicBool>,
    config_udp_vid: AppConfig,
) -> Result<(), AppError> {
    let socket = UdpSocket::bind(format!("0.0.0.0:{}", config_udp_vid.udp_port_vid))?;
    // Receives a single datagram message on the socket. If `buf` is too small to hold
    // the message, it will be cut off.
    let mut buf = [0; BUFFER_MAX_UDP_SIZE];
        
    let mut reassembler = FragmentReassembler::new(ReassemblyMode::Volatile);

    loop {
        let (amt, _) = socket.recv_from(&mut buf)?;
        
        // Extraction du header de la vidéo
        let header = match HeaderUdpVid::header_vid_parse(&buf[..amt]) {
            Ok(h) => h,
            Err(_) => continue,
        };

        // Extraction du payload (on vire le header)
        let payload = buf[HEADER_VID_SIZE..amt].to_vec();

        // On nourrit le réassembleur
        if let Some(full_image) = reassembler.push_fragment(
            header.frame_id,
            header.frag_idx as usize,
            header.frag_total as usize,
            payload,
        ) {
            // Le puzzle est complet ! On envoie l'image au GUI
            if tx_img.send(full_image).is_ok() {
                camera_connected.store(true, Ordering::Relaxed);
            }
        }
    }
}