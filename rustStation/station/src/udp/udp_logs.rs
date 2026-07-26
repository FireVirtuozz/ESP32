use std::{collections::VecDeque, net::UdpSocket, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Sender}, thread, time::Instant};

use log::{debug, error, warn};

use crate::{config::AppConfig, error::AppError, gui::screens::logs::LogPacket, sensors::{TelemetryEnum, TelemetryPacket, parser::{SENSORS_HEADER_SIZE, SensorsUdpHeader, parse_buffer_hall, parse_buffer_ina, parse_buffer_mpu, parse_buffer_ultrasonic}}};

const MAX_SIZE_LOGS_BUF: usize = 1400;

//return Result, allows us to use ? error propagation in fn 
pub fn udp_logs_server_init(
    tx_log: Sender<LogPacket>,
    logs_connected: Arc<AtomicBool>,
    config_udp_recv: AppConfig,
) -> thread::JoinHandle<()> {

    let handle = thread::spawn(move || {
        if let Err(e) = udp_logs_loop(tx_log, logs_connected, config_udp_recv) {
            error!("UDP error recv: {:?}", e);
        }
    });
    handle
}

fn udp_logs_loop(
    tx_log: Sender<LogPacket>,
    logs_connected: Arc<AtomicBool>,
    config_udp_recv: AppConfig,
) -> Result<(), AppError> {
    let socket = UdpSocket::bind(format!("0.0.0.0:{}", config_udp_recv.udp_port_logs))?;

    let relay_socket = if config_udp_recv.is_relay_tailscale {
        Some(UdpSocket::bind("0.0.0.0:0")?)
    } else {
        None
    };
    // Receives a single datagram message on the socket. If `buf` is too small to hold
    // the message, it will be cut off.
    let mut buf = [0; MAX_SIZE_LOGS_BUF];
    loop {
        
        let (amt, src) = socket.recv_from(&mut buf)?;

        let buf = &buf[..amt]; //reference only to received data

        if let Some(sock) = &relay_socket {
            for addr in &config_udp_recv.tailscale_addresses {
                if let Err(e) = sock.send_to(buf, format!("{}:{}", 
                    addr,
                    config_udp_recv.udp_port_logs)) 
                {
                    warn!("Relay failed to {}: {:?}", addr, e);
                }
            }
        }

        debug!("buf raw received : {:?}, size: {}, from: {:?}", buf, amt, src);
        debug!("{}", String::from_utf8_lossy(buf));

        logs_connected.store(true, Ordering::Relaxed);
        tx_log.send(LogPacket::log_from_buffer(&buf[.. amt])?)?;
    }
}