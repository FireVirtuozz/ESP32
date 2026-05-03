use std::collections::VecDeque;
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
    let mut dump_log = VecDeque::<LogPacket>::new();
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
                //println!("Frame type of monitor");
                if frame_udp.flags & 0b00000001 == 0b00000001 {
                    //ky
                    let frame_ky = parse_buffer_hall(&buf[offset_size..offset_size + KY_SIZE])?;
                    offset_size += KY_SIZE;
                    //println!("frame KY: {:?}", frame_ky);
                    packet_telem.hall = Some(frame_ky);
                }
                if frame_udp.flags & 0b00000010 == 0b00000010 {
                    //hc
                    let frame_hc = parse_buffer_ultrasonic(&buf[offset_size..offset_size + HC_SIZE])?;
                    offset_size += HC_SIZE;
                    //println!("frame HC: {:?}", frame_hc);
                    //frame_hc.print_hc();
                    packet_telem.ultrasonic = Some(frame_hc);
                }
                if frame_udp.flags & 0b00000100 == 0b00000100 {
                    //mpu
                    let frame_mpu = parse_buffer_mpu(&buf[offset_size..offset_size + MPU_SIZE])?;
                    offset_size += MPU_SIZE;
                    //println!("frame MPU: {:?}", frame_mpu);
                    //frame_mpu.print_imu();
                    packet_telem.imu = Some(frame_mpu);
                }
                if frame_udp.flags & 0b00001000 == 0b00001000 {
                    //ina
                    let frame_ina = parse_buffer_ina(&buf[offset_size..offset_size + INA_SIZE])?;
                    offset_size += INA_SIZE;
                    //println!("frame INA: {:?}", frame_ina);
                    //frame_ina.print_ina();
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
                //println!("msg: {:?}", log_pck.msg.as_ref().unwrap());
                tx_log.send(log_pck)?;
            },
            2 => {
                logs_connected.store(true, Ordering::Relaxed);
                let msg_id = &buf[HEADER_SIZE];
                let msg_bytes = &buf[HEADER_SIZE + 1..amt];
                let log_pck = LogPacket {
                    msg: Some(String::from_utf8_lossy(msg_bytes).to_string()),
                };
                println!("msg[{msg_id}]: {:?}", log_pck.msg.as_ref().unwrap());
                dump_log.push_back(log_pck);
                if *msg_id == 0 {
                    while let Some(log_pck) = dump_log.pop_front() {
                        tx_log.send(log_pck)?;
                    }
                }
            }
            _ => return Err("Invalid frame type".into()),
        }
    }
}

/*
 1 │ kage version : 1

   2 │[CHIP INFO][CHIP INFO]
Model        : ESP32
Cores        : 2
Revision     : 301
WiFi 2.4GHz=2 BT Classic=32 BLE=16
EMB_FLASH=0 IEEE 802.15.4=0 PSRAM=0

   3 │[MEMORY][MEMORY]
Free heap          : 187656 bytes
Free internal heap : 187656 bytes
Minimum free heap  : 185348 bytes

   4 │[MAC][MAC]
Base MAC  : B0:CB:D8:E8:68:38
EFUSE MAC : B0:CB:D8:E8:68:38

   5 │[CPU][CPU]
Cycle cnt  : 3403385375
Stack ptr  : 0x3ffbad10
Core ID    : 0
Enabled IRQ mask   : 0x1300006F
Debugger attached  : 0
Thread ptr         : 0x3ffbadf8
Privilege level    : -1

   6 │ reserv1[1]  : 0x0
Minimal efuse block  : 0
Maximal efuse block  : 99
MMU page size (log2)  : 16
ELF SHA256    : c90a59058

   7 │ Min chip rev    : 0 (legacy)
Min chip rev    : 0.0 (full)
Max chip rev    : 3.99 (full)
Reserved        : [0x00, 0x00, 0x00, 0x00]
Hash appended   : yes (SHA256)

   8 │[BOOTLOADER][BOOTLOADER]
Magic byte      : 0x50 (valid)
Secure version  : 0
Version         : 1
IDF version     : v6.1-dev-4182-g47faecc3e4
Compile time    : May  1 2026 13:00:15
Reserved        : [0x00, 0x00]
Reserved2       : [0x00, 0x00, 0x00, 0x00, ...]

   9 │[EFUSE GLOBAL][EFUSE GLOBAL]
Flash encryption : ENABLED
Secure version   : 0
Secure version valid : Yes
Pkg version      : 1
eFuse errors     : none

  10 │: 0xFA (size: 8)
CODING_SCHEME        : NONE (size: 2)

  11 │ K3_PART_RESERVE    : no (size: 1)
CHIP_CPU_FREQ_RATED  : YES (size: 1)
CHIP_CPU_FREQ_LOW    : no (size: 1)

  12 │: 0 (size: 5)
SPI_PAD_CS0          : 0 (size: 5)
SPI_PAD_HD           : 0 (size: 5)

  13 │[EFUSE BLK1  (FLASH ENCRYPTION)][EFUSE BLK1  (FLASH ENCRYPTION)]
scheme: NONE                 | empty: yes
  [0] 0x00000000
  [1] 0x00000000
  [2] 0x00000000
  [3] 0x00000000
  [4] 0x00000000
  [5] 0x00000000
  [6] 0x00000000
  [7] 0x00000000

  14 │[EFUSE BLK2  (SECURE BOOT)][EFUSE BLK2  (SECURE BOOT)]
scheme: NONE                 | empty: yes
  [0] 0x00000000
  [1] 0x00000000
  [2] 0x00000000
  [3] 0x00000000
  [4] 0x00000000
  [5] 0x00000000
  [6] 0x00000000
  [7] 0x00000000

  15 │[EFUSE BLK3  (USER)][EFUSE BLK3  (USER)]
scheme: NONE                 | empty: yes
  [0] 0x00000000
  [1] 0x00000000
  [2] 0x00000000
  [3] 0x00000000
  [4] 0x00000000
  [5] 0x00000000
  [6] 0x00000000
  [7] 0x00000000

  16 │[EFUSE KEY BLK1][EFUSE KEY BLK1]
purpose         : FLASH ENCRYPTION
purpose find    : found (BLK1)
unused          : no
read  protect   : YES
write protect   : YES
purpose wr dis  : YES

  17 │[EFUSE KEY BLK2][EFUSE KEY BLK2]
purpose         : SECURE BOOT
purpose find    : found (BLK2)
unused          : yes
read  protect   : no
write protect   : no
purpose wr dis  : YES

  18 │[EFUSE KEY BLK3][EFUSE KEY BLK3]
purpose         : USER
purpose find    : found (BLK3)
unused          : yes
read  protect   : no
write protect   : no
purpose wr dis  : YES

  19 │[EVENT LOOP][EVENT LOOP]

  20 │ locks:
ID   Vaddr Start   Vaddr End    Block Size   Caps   Paddr Start   Paddr End
*/