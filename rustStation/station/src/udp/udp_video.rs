use std::{collections::HashMap, net::UdpSocket, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Sender}, thread};

use log::{debug, error};

use crate::{config::AppConfig, error::AppError, udp::{BUFFER_MAX_UDP_SIZE, FragmentReassembler, HEADER_FRAGMENT_SIZE, HeaderUdpFragment, ReassemblyMode}};




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
        let header = match HeaderUdpFragment::header_fragment_parse(&buf[..amt]) {
            Ok(h) => h,
            Err(_) => continue,
        };

        // Extraction du payload (on vire le header)
        let payload = buf[HEADER_FRAGMENT_SIZE..amt].to_vec();

        // On nourrit le réassembleur
        if let Some(full_image) = reassembler.push_fragment(
            header,
            payload,
        ) {
            // Le puzzle est complet ! On envoie l'image au GUI
            if tx_img.send(full_image).is_ok() {
                camera_connected.store(true, Ordering::Relaxed);
            }
        }
    }
}