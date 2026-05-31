use std::{collections::HashMap, net::UdpSocket, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Sender}, thread};

use egui::collapsing_header::HeaderResponse;
use log::{debug, error};

use crate::{config::AppConfig, error::AppError, gui::screens::dump::DumpEntry, udp::{BUFFER_MAX_UDP_SIZE, FragmentReassembler, HEADER_FRAGMENT_SIZE, HeaderUdpFragment, ReassemblyMode}};

pub fn udp_server_dump_init(
    tx_dump: Sender<DumpEntry>,
    config_udp_dump: AppConfig,
) -> thread::JoinHandle<()> {

    let handle = thread::spawn(move || {
        if let Err(e) = udp_server_dump(tx_dump, config_udp_dump) {
            error!("UDP error dump: {:?}", e);
        }
    });
    handle
}



fn udp_server_dump(
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

    // Extraction du header de la vidéo
        let header = match HeaderUdpFragment::header_fragment_parse(&buf[..amt]) {
            Ok(h) => h,
            Err(_) => continue,
        };

        let payload = buf[HEADER_FRAGMENT_SIZE..amt].to_vec();

        // On pousse dans la machine générique
        if let Some(full_dump_bytes) = reassembler.push_fragment(header, payload) {
            // Le puzzle est complet ! On a TOUS les octets du dump.
            // On les passe au parseur pour fabriquer notre belle struct pleine de Strings
            if let Some(dump_entry) = DumpEntry::parse_from_buf(&full_dump_bytes) {
                // On envoie la struct finale bien propre à Egui
                let _ = tx_dump.send(dump_entry);
            }
        }
    }
}