use std::{collections::VecDeque, net::UdpSocket, println, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Sender}, thread, time::Instant};

use log::{debug, error, info, warn};

use crate::{config::{self, AppConfig}, error::AppError, gui::screens::logs::LogPacket, sensors::{EspPacket, PacketAcs758, PacketBatAdc, PacketBz181, PacketImu, PacketKy033, PacketRcwl0515, PacketRfidRc522, SensorType, TelemetryEnum, TelemetryPacket, lidar::{LidarD500Batch, LidarD500Packet}, parser::{SENSORS_HEADER_SIZE, SensorsUdpHeader, parse_buffer_bmp, parse_buffer_bno085_accel, parse_buffer_bno085_classif, parse_buffer_bno085_linear_accel, parse_buffer_bno085_rotation, parse_buffer_brake, parse_buffer_dht11, parse_buffer_esp, parse_buffer_hall, parse_buffer_ina, parse_buffer_motor, parse_buffer_mpu, parse_buffer_photosensor, parse_buffer_pong, parse_buffer_ultrasonic, parse_buffer_vl53l1x}}, udp::BUFFER_MAX_UDP_SIZE};

//return Result, allows us to use ? error propagation in fn 
pub fn udp_sensors_server_init(
    tx: crossbeam_channel::Sender<TelemetryPacket>,
    sensors_connected: Arc<AtomicBool>,
    config_udp_recv: AppConfig,
    start_instant: Instant,
    tx_record: Sender<(TelemetryPacket, f64)>,
    tx_integ: crossbeam_channel::Sender<TelemetryPacket>,
    tx_odo: crossbeam_channel::Sender<TelemetryPacket>,
    tx_lidar: crossbeam_channel::Sender<TelemetryPacket>,
) -> thread::JoinHandle<()> {

    let handle = thread::spawn(move || {
        if let Err(e) = udp_sensors_loop(tx, sensors_connected, config_udp_recv, start_instant, tx_record, 
            tx_integ, tx_odo, tx_lidar) {
            error!("UDP error recv: {:?}", e);
        }
    });
    handle
}

fn udp_sensors_loop(
    tx: crossbeam_channel::Sender<TelemetryPacket>,
    sensors_connected: Arc<AtomicBool>,
    config_udp_recv: AppConfig,
    start_instant: Instant,
    tx_record: Sender<(TelemetryPacket, f64)>,
    tx_integ: crossbeam_channel::Sender<TelemetryPacket>,
    tx_odo: crossbeam_channel::Sender<TelemetryPacket>,
    tx_lidar: crossbeam_channel::Sender<TelemetryPacket>,
) -> Result<(), AppError> {
    let socket = UdpSocket::bind(format!("0.0.0.0:{}", config_udp_recv.udp_port_sensors))?;

    // Receives a single datagram message on the socket. If `buf` is too small to hold
    // the message, it will be cut off.
    let mut buf = [0; BUFFER_MAX_UDP_SIZE];
    loop {
        
        let (amt, src) = match socket.recv_from(&mut buf) {
            Ok(r) => r,
            Err(e) => { error!("recv_from failed on sensors: {e}"); continue; }
        };

        let buf = &buf[..amt]; //reference only to received data

        let ts = start_instant.elapsed().as_secs_f64();

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
                tx_integ.send(packet.clone())?;
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
                            pulses: buf[amt - 4],
                            motor: i16::from_le_bytes(buf[amt - 3 .. amt - 1].try_into().expect("ky033 motor")),
                            sign_motor_positive : match buf[amt - 1] {0 => false, 1 => true, _ => false},
                        }
                    )
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx_odo.send(packet.clone())?;
                tx.send(packet)?;
            },
            SensorType::Esp => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::ESP(parse_buffer_esp(&buf[SENSORS_HEADER_SIZE .. amt]).expect("esp"))
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
            SensorType::Motor => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::MOTOR(parse_buffer_motor(&buf[SENSORS_HEADER_SIZE .. amt])?)
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Brake => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::BRAKE(parse_buffer_brake(&buf[SENSORS_HEADER_SIZE .. amt])?)
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Bmp280 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::BMP(parse_buffer_bmp(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Dht11 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::DHT11(parse_buffer_dht11(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Ky018 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::PHOTOSENSOR(parse_buffer_photosensor(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Vl53l1x => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::VL53L1X(parse_buffer_vl53l1x(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx_odo.send(packet.clone())?;
                tx.send(packet)?;
            },
            SensorType::Bno085Accel => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::BNO085ACCEL(parse_buffer_bno085_accel(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Bno085Rot => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::BNO085ROTATION(parse_buffer_bno085_rotation(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx_odo.send(packet.clone())?;
                tx.send(packet)?;
            },
            SensorType::Bno085LinearAccel => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::BNO085LINEARACCEL(parse_buffer_bno085_linear_accel(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Bno085Stability => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::BNO085CLASSIF(parse_buffer_bno085_classif(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                debug!("{:?}", packet);
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::LidarD500 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::LIDARD500(LidarD500Batch::from_bytes(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx_lidar.send(packet.clone())?;
                tx.send(packet)?;
            },
            SensorType::BatAdc => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::BATADC(PacketBatAdc::from_bytes(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Acs758 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::ACS758(PacketAcs758::from_bytes(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            },
            SensorType::Bz181 => {
                sensors_connected.store(true, Ordering::Relaxed);
                let packet = TelemetryPacket {
                    hd_info: frame_udp_header,
                    packet: TelemetryEnum::BZ181(PacketBz181::from_bytes(&buf[SENSORS_HEADER_SIZE .. amt])?),
                };
                if config_udp_recv.recording {
                    let _ = tx_record.send((packet.clone(), ts));
                }
                tx.send(packet)?;
            }
            _ => return Err("Invalid frame type".into()),
        }
    }
}