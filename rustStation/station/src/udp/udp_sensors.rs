use std::{collections::VecDeque, net::UdpSocket, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Sender}, thread};

use log::{debug, error};

use crate::{config::AppConfig, error::AppError, gui::screens::logs::LogPacket, sensors::{TelemetryEnum, TelemetryPacket, parser::{SENSORS_HEADER_SIZE, SensorsUdpHeader, parse_buffer_hall, parse_buffer_ina, parse_buffer_mpu, parse_buffer_ultrasonic}}, udp::udp_video::HeaderUdpVid};

const MAX_SIZE_TELEMETRY_BUF: usize = 256;

//return Result, allows us to use ? error propagation in fn 
pub fn udp_sensors_server_init(
    tx: Sender<TelemetryPacket>,
    sensors_connected: Arc<AtomicBool>,
    config_udp_recv: AppConfig,
) -> thread::JoinHandle<()> {

    let handle = thread::spawn(move || {
        if let Err(e) = udp_sensors_loop(tx, sensors_connected, config_udp_recv) {
            error!("UDP error recv: {:?}", e);
        }
    });
    handle
}

fn udp_sensors_loop(
    tx: Sender<TelemetryPacket>,
    sensors_connected: Arc<AtomicBool>,
    config_udp_recv: AppConfig,
) -> Result<(), AppError> {
    let socket = UdpSocket::bind(format!("0.0.0.0:{}", config_udp_recv.udp_port_sensors))?;
    // Receives a single datagram message on the socket. If `buf` is too small to hold
    // the message, it will be cut off.
    let mut buf = [0; MAX_SIZE_TELEMETRY_BUF];
    let mut dump_log = VecDeque::<LogPacket>::new();
    loop {
        
        let (amt, src) = socket.recv_from(&mut buf)?;

        let buf = &buf[..amt]; //reference only to received data

        debug!("buf raw received : {:?}, size: {}, from: {:?}", buf, amt, src);
        debug!("{}", String::from_utf8_lossy(buf));

        let frame_udp_header = SensorsUdpHeader::header_from_buffer(buf)?;

        match frame_udp_header.ftype {
            0 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::KY003(parse_buffer_hall(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                tx.send(packet)?;
            },
            1 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::HCSR04(parse_buffer_ultrasonic(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                tx.send(packet)?;
            },
            2 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::MPU(parse_buffer_mpu(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                tx.send(packet)?;
            },
            3 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::INA226(parse_buffer_ina(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                tx.send(packet)?;
            },
            _ => return Err("Invalid frame type".into()),
        }
    }
}