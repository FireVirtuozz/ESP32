use std::{collections::VecDeque, net::UdpSocket, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Sender}, thread, time::Instant};

use log::{debug, error, warn};

use crate::{config::AppConfig, error::AppError, gui::screens::logs::LogPacket, sensors::{TelemetryEnum, TelemetryPacket, parser::{SENSORS_HEADER_SIZE, SensorsUdpHeader, parse_buffer_hall, parse_buffer_ina, parse_buffer_mpu, parse_buffer_ultrasonic}}, udp::BUFFER_MAX_UDP_SIZE};

//return Result, allows us to use ? error propagation in fn 
pub fn udp_tailscale_redirect(
    config_udp_recv: AppConfig,
) -> Vec<thread::JoinHandle<()>> {

    let ports = [config_udp_recv.udp_port_logs, config_udp_recv.udp_port_dump, 
        config_udp_recv.udp_port_vid, config_udp_recv.udp_port_sensors];
    
    let mut handles = vec![];

    for port in ports {
        let config = config_udp_recv.clone();
        handles.push(thread::spawn(move || {
            if let Err(e) = udp_port_loop(config, port) {
                error!("UDP error recv: {:?}", e);
            }}
        ));
    }
    handles
}

fn udp_port_loop(
    config_udp_recv: AppConfig,
    port: u16,
) -> Result<(), AppError> {
    let socket = UdpSocket::bind(format!("0.0.0.0:{}", port))?;

    let relay_socket = UdpSocket::bind("0.0.0.0:0")?;

    // Receives a single datagram message on the socket. If `buf` is too small to hold
    // the message, it will be cut off.
    let mut buf = [0; BUFFER_MAX_UDP_SIZE];
    loop {
        
        let (amt, src) = match socket.recv_from(&mut buf) {
            Ok(r) => r,
            Err(e) => { error!("recv_from failed on port {port}: {e}"); continue; }
        };

        let buf = &buf[..amt]; //reference only to received data

        for addr in &config_udp_recv.tailscale_addresses {
            if let Err(e) = relay_socket.send_to(buf, format!("{}:{}", addr, port)) 
            {
                warn!("Relay failed to {}: {:?}", addr, e);
            }
        }
    }
}