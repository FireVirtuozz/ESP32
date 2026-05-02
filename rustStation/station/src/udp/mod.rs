use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::Sender;
use std::{error::Error, net::UdpSocket};
use std::thread;
use crate::error::AppError;
use crate::monitor::{LogPacket, PacketImu, TelemetryPacket};
use crate::monitor::{HC_SIZE, INA_SIZE, KY_SIZE, MPU_SIZE, parser::{self, FrameUdpHeader, HEADER_SIZE, parse_buffer_hall, parse_buffer_ina, parse_buffer_mpu, parse_buffer_ultrasonic}};

//return Result, allows us to use ? error propagation in fn 
pub fn udp_server_init(
    tx: Sender<TelemetryPacket>,
    tx_log: Sender<LogPacket>,
    sensors_connected: Arc<AtomicBool>,
    logs_connected: Arc<AtomicBool>
) -> thread::JoinHandle<()> {

    let handle = thread::spawn(move || {
        if let Err(e) = udp_loop(tx, tx_log, sensors_connected, logs_connected) {
            eprintln!("UDP error: {:?}", e);
        }
    });
    handle
}

fn udp_loop(
    tx: Sender<TelemetryPacket>,
    tx_log: Sender<LogPacket>,
    sensors_connected: Arc<AtomicBool>,
    logs_connected: Arc<AtomicBool>
) -> Result<(), AppError> {
    let socket = UdpSocket::bind("192.168.4.2:34254")?;
    // Receives a single datagram message on the socket. If `buf` is too small to hold
    // the message, it will be cut off.
    let mut buf = [0; 256];
    loop {
        
        let (amt, src) = socket.recv_from(&mut buf)?;

        let buf = &buf[..amt]; //reference only to received data

        //println!("buf raw received : {:?}, size: {}, from: {:?}", buf, amt, src);
        //println!("{}", String::from_utf8_lossy(buf));

        let frame_udp = FrameUdpHeader::header_from_buffer(buf)?;

        match frame_udp.ftype {
            0 => {
                let mut packet_telem = TelemetryPacket {
                    hall: None,
                    ina: None,
                    imu: None,
                    ultrasonic: None,
                };

                sensors_connected.store(true, Ordering::Relaxed);
                let mut offset_size = HEADER_SIZE;
                println!("Frame type of monitor");
                if frame_udp.flags & 0b00000001 == 0b00000001 {
                    //ky
                    let frame_ky = parse_buffer_hall(&buf[offset_size..offset_size + KY_SIZE])?;
                    offset_size += KY_SIZE;
                    println!("frame KY: {:?}", frame_ky);
                    packet_telem.hall = Some(frame_ky);
                }
                if frame_udp.flags & 0b00000010 == 0b00000010 {
                    //hc
                    let frame_hc = parse_buffer_ultrasonic(&buf[offset_size..offset_size + HC_SIZE])?;
                    offset_size += HC_SIZE;
                    println!("frame HC: {:?}", frame_hc);
                    frame_hc.print_hc();
                    packet_telem.ultrasonic = Some(frame_hc);
                }
                if frame_udp.flags & 0b00000100 == 0b00000100 {
                    //mpu
                    let frame_mpu = parse_buffer_mpu(&buf[offset_size..offset_size + MPU_SIZE])?;
                    offset_size += MPU_SIZE;
                    println!("frame MPU: {:?}", frame_mpu);
                    frame_mpu.print_imu();
                    packet_telem.imu = Some(frame_mpu);
                }
                if frame_udp.flags & 0b00001000 == 0b00001000 {
                    //ina
                    let frame_ina = parse_buffer_ina(&buf[offset_size..offset_size + INA_SIZE])?;
                    offset_size += INA_SIZE;
                    println!("frame INA: {:?}", frame_ina);
                    frame_ina.print_ina();
                    packet_telem.ina = Some(frame_ina);
                }
                if frame_udp.flags & 0b00010000 == 0b00010000 {
                    //esp
                }
                tx.send(packet_telem)?;
            },
            1 => {
                logs_connected.store(true, Ordering::Relaxed);
                let msg_bytes = &buf[HEADER_SIZE..amt];
                let log_pck = LogPacket {
                    msg: Some(String::from_utf8_lossy(msg_bytes).to_string()),
                };
                println!("msg: {:?}", log_pck.msg.as_ref().unwrap());
                tx_log.send(log_pck)?;
            },
            _ => return Err("Invalid frame type".into()),
        }
    }
}