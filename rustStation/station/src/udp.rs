use std::{error::Error, net::UdpSocket};
use crate::monitor::{HC_SIZE, INA_SIZE, KY_SIZE, MPU_SIZE, parser::{self, FrameUdpHeader, HEADER_SIZE, parse_buffer_hall, parse_buffer_ina, parse_buffer_mpu, parse_buffer_ultrasonic}};

//return Result, allows us to use ? error propagation in fn 
pub fn udp_server_init() -> Result<(), Box<dyn Error>> {
    {
        let socket = UdpSocket::bind("192.168.4.2:34254")?;

        // Receives a single datagram message on the socket. If `buf` is too small to hold
        // the message, it will be cut off.
        let mut buf = [0; 256];
        let (amt, src) = socket.recv_from(&mut buf)?;

        let buf = &buf[..amt]; //reference only to received data
        let payload = &buf[HEADER_SIZE..amt];

        println!("buf raw received : {:?}, size: {}, from: {:?}", buf, amt, src);
        println!("{}", String::from_utf8_lossy(buf));

        let frameUdp = FrameUdpHeader::header_from_buffer(buf)?;

        match frameUdp.ftype {
            0 => {
                let mut offset_size = HEADER_SIZE;
                println!("Frame type of monitor");
                if frameUdp.flags & 0b00000001 == 0b00000001 {
                    //ky
                    let frame_ky = parse_buffer_hall(&buf[offset_size..offset_size + KY_SIZE])?;
                    offset_size += KY_SIZE;
                    println!("frame KY: {:?}", frame_ky);
                }
                if frameUdp.flags & 0b00000010 == 0b00000010 {
                    //hc
                    let frame_hc = parse_buffer_ultrasonic(&buf[offset_size..offset_size + HC_SIZE])?;
                    offset_size += HC_SIZE;
                    println!("frame HC: {:?}", frame_hc);
                    frame_hc.print_hc();
                }
                if frameUdp.flags & 0b00000100 == 0b00000100 {
                    //mpu
                    let frame_mpu = parse_buffer_mpu(&buf[offset_size..offset_size + MPU_SIZE])?;
                    offset_size += MPU_SIZE;
                    println!("frame MPU: {:?}", frame_mpu);
                    frame_mpu.print_imu();
                }
                if frameUdp.flags & 0b00001000 == 0b00001000 {
                    //ina
                    let frame_ina = parse_buffer_ina(&buf[offset_size..offset_size + INA_SIZE])?;
                    offset_size += INA_SIZE;
                    println!("frame INA: {:?}", frame_ina);
                    frame_ina.print_ina();
                }
                if frameUdp.flags & 0b00010000 == 0b00010000 {
                    //esp
                }
            },
            _ => return Err("Invalid frame type".into()),
        }
        
    } // the socket is closed here
    Ok(())
}