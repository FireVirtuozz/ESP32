use std::{collections::VecDeque, net::UdpSocket, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Sender}, thread, time::Instant};

use log::{debug, error, info, warn};

use crate::{config::AppConfig, error::AppError, gui::screens::logs::LogPacket, sensors::{EspPacket, PacketKy033, PacketRcwl0515, PacketRfidRc522, SensorType, TelemetryEnum, TelemetryPacket, parser::{SENSORS_HEADER_SIZE, SensorsUdpHeader, parse_buffer_esp, parse_buffer_hall, parse_buffer_ina, parse_buffer_mpu, parse_buffer_pong, parse_buffer_ultrasonic}}};

const MAX_SIZE_TELEMETRY_BUF: usize = 256;

//return Result, allows us to use ? error propagation in fn 
pub fn udp_sensors_server_init(
    tx: Sender<TelemetryPacket>,
    sensors_connected: Arc<AtomicBool>,
    config_udp_recv: AppConfig,
    start_instant: Instant,
    tx_record: Sender<(TelemetryPacket, f64)>,
) -> thread::JoinHandle<()> {

    let handle = thread::spawn(move || {
        if let Err(e) = udp_sensors_loop(tx, sensors_connected, config_udp_recv, start_instant, tx_record) {
            error!("UDP error recv: {:?}", e);
        }
    });
    handle
}

fn udp_sensors_loop(
    tx: Sender<TelemetryPacket>,
    sensors_connected: Arc<AtomicBool>,
    config_udp_recv: AppConfig,
    start_instant: Instant,
    tx_record: Sender<(TelemetryPacket, f64)>,
) -> Result<(), AppError> {
    let socket = UdpSocket::bind(format!("0.0.0.0:{}", config_udp_recv.udp_port_sensors))?;
    // Receives a single datagram message on the socket. If `buf` is too small to hold
    // the message, it will be cut off.
    let mut buf = [0; MAX_SIZE_TELEMETRY_BUF];
    loop {
        
        let (amt, src) = socket.recv_from(&mut buf)?;
        let ts = start_instant.elapsed().as_secs_f64();

        let buf = &buf[..amt]; //reference only to received data

        debug!("buf raw received : {:?}, size: {}, from: {:?}", buf, amt, src);
        debug!("{}", String::from_utf8_lossy(buf));

        let frame_udp_header = SensorsUdpHeader::header_from_buffer(buf)?;

        match frame_udp_header.ftype {
            SensorType::Ky003 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::KY003(parse_buffer_hall(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Hcsr04 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::HCSR04(parse_buffer_ultrasonic(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Mpu9250 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::MPU(parse_buffer_mpu(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Ina226 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::INA226(parse_buffer_ina(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::RfidRc522 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::RFIDRC522(
                        PacketRfidRc522 {
                            uid : (&buf[SENSORS_HEADER_SIZE .. amt]).to_vec(),
                        }),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Rcwl0515 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::RCWL0515(
                        PacketRcwl0515 {
                            detection : buf[amt - 1] != 0,
                        }),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Ky033 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::KY033(
                        PacketKy033 {
                            pulses: buf[amt - 2],
                            motor: buf[amt - 1] as i8,
                        }
                    )
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Esp => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::ESP(parse_buffer_esp(&buf[SENSORS_HEADER_SIZE .. amt])?)
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Pong => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::PONG(parse_buffer_pong(&buf[SENSORS_HEADER_SIZE .. amt], start_instant)?)
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            _ => return Err("Invalid frame type".into()),
        }
    }
}