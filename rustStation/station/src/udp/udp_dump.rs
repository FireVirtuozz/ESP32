use std::{collections::HashMap, net::UdpSocket, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Sender}, thread};

use log::{debug, error};

use crate::{config::AppConfig, error::AppError, gui::screens::dump::DumpEntry, udp::{BUFFER_MAX_UDP_SIZE, FragmentReassembler, ReassemblyMode}};

pub fn udp_server_dump_init(
    tx_dump: Sender<DumpEntry>,
    config_udp_dump: AppConfig,
) -> thread::JoinHandle<()> {

    let handle = thread::spawn(move || {
        if let Err(e) = udp_video_dump(tx_dump, config_udp_dump) {
            error!("UDP error vid: {:?}", e);
        }
    });
    handle
}



fn udp_video_dump(
    tx_dump: Sender<DumpEntry>,
    config_udp_dump: AppConfig,
) -> Result<(), AppError> {
    let socket = UdpSocket::bind(format!("0.0.0.0:{}", config_udp_dump.udp_port_dump))?;
    // Receives a single datagram message on the socket. If `buf` is too small to hold
    // the message, it will be cut off.
    let mut buf = [0; BUFFER_MAX_UDP_SIZE];
        
    // Initialisation du réassembleur en mode STRICT 🛡️
    let mut reassembler = FragmentReassembler::new(ReassemblyMode::Strict);

    loop {
        let (amt, _) = socket.recv_from(&mut buf)?;
        if amt < 6 { continue; }

        // Extraction du mini-header de fragment pour le dump
        let dump_id = u16::from_le_bytes([buf[0], buf[1]]) as u32;
        let frag_idx = u16::from_le_bytes([buf[2], buf[3]]) as usize;
        let frag_total = u16::from_le_bytes([buf[4], buf[5]]) as usize;

        let payload = buf[6..amt].to_vec();

        // On pousse dans la machine générique
        if let Some(full_dump_bytes) = reassembler.push_fragment(
            dump_id,
            frag_idx,
            frag_total,
            payload,
        ) {
            // Le puzzle est complet ! On a TOUS les octets du dump.
            // On les passe au parseur pour fabriquer notre belle struct pleine de Strings
            if let Some(dump_entry) = DumpEntry::parse_from_buf(&full_dump_bytes) {
                // On envoie la struct finale bien propre à Egui
                let _ = tx_dump.send(dump_entry);
            }
        }
    }
}