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
 1 │[EFUSE REVISION][EFUSE REVISION]
Factory MAC address : B0:CB:D8:E8:68:38
chip block revision : 0
flash encryption enabled : Yes
Skip maximum version check : No
Skip block version check : No
chip revision : 301
major chip version : 3
minor chip version : 1
chip pac

   2 │ kage version : 1

   3 │[CHIP INFO][CHIP INFO]
Model        : ESP32
Cores        : 2
Revision     : 301
WiFi 2.4GHz=2 BT Classic=32 BLE=16
EMB_FLASH=0 IEEE 802.15.4=0 PSRAM=0

   4 │[MEMORY][MEMORY]
Free heap          : 188648 bytes
Free internal heap : 188648 bytes
Minimum free heap  : 185500 bytes

   5 │[MAC][MAC]
Base MAC  : B0:CB:D8:E8:68:38
EFUSE MAC : B0:CB:D8:E8:68:38

   6 │[CPU][CPU]
Cycle cnt  : 1804159361
Stack ptr  : 0x3ffbad10
Core ID    : 0
Enabled IRQ mask   : 0x1300006F
Debugger attached  : 0
Thread ptr         : 0x3ffbadf8
Privilege level    : -1

   7 │[APP][APP]
ESP-IDF version    : v6.1-dev-4182-g47faecc3e4
Project name  : server
App version   : 1.7.0
IDF version   : v6.1-dev-4182-g47faecc3e4
Compile time  : May  1 2026 13:00:15
Magic word  : 0xABCD5432 (valid)
Secure version  : 0
reserv1[0]  : 0x0

   8 │ reserv1[1]  : 0x0
Minimal efuse block  : 0
Maximal efuse block  : 99
MMU page size (log2)  : 16
ELF SHA256    : 69e7fae4e

   9 │[APP HEADER][APP HEADER]
Magic           : 0xE9 (valid)
Segment count   : 6
SPI mode        : DIO
SPI speed       : DIV_2
SPI size        : 4 MB
Entry addr      : 0x40081448
WP pin          : 0xEE (disabled)
SPI pin drv     : [0, 0, 0]
Chip ID         : ESP32

  10 │ Min chip rev    : 0 (legacy)
Min chip rev    : 0.0 (full)
Max chip rev    : 3.99 (full)
Reserved        : [0x00, 0x00, 0x00, 0x00]
Hash appended   : yes (SHA256)

  11 │[BOOTLOADER][BOOTLOADER]
Magic byte      : 0x50 (valid)
Secure version  : 0
Version         : 1
IDF version     : v6.1-dev-4182-g47faecc3e4
Compile time    : May  1 2026 13:00:15
Reserved        : [0x00, 0x00]
Reserved2       : [0x00, 0x00, 0x00, 0x00, ...]
 12 │[EFUSE GLOBAL][EFUSE GLOBAL]
Flash encryption : ENABLED
Secure version   : 0
Secure version valid : Yes
Pkg version      : 1
eFuse errors     : none

  13 │[EFUSE BLK0  (RESERVED)][EFUSE BLK0  (RESERVED)]
scheme: NONE                 | empty: no
CHIP_VER_REV1        : 1 (size: 1)
CHIP_VER_REV2        : 1 (size: 1)
WAFER_VERSION_MINOR  : 1 (size: 2)
CHIP_PACKAGE         : 1 (size: 3)
CHIP_PACKAGE_4BIT    : 0 (size: 1)
MAC_CRC

  14 │: 0xFA (size: 8)
CODING_SCHEME        : NONE (size: 2)

  15 │[EFUSE BLK0 SECURITY][EFUSE BLK0 SECURITY]
JTAG_DISABLE         : YES (size: 1)
ABS_DONE_0           : no (size: 1)
ABS_DONE_1           : no (size: 1)
DISABLE_DL_ENCRYPT   : no (size: 1)
DISABLE_DL_DECRYPT   : YES (size: 1)
DISABLE_DL_CACHE     : YES (size: 1)
UART_DO

  16 │ WNLOAD_DIS    : no (size: 1)
KEY_STATUS           : no (size: 1)
CONSOLE_DEBUG_DIS    : YES (size: 1)
DISABLE_SDIO_HOST    : no (size: 1)
DISABLE_APP_CPU      : no (size: 1)
DISABLE_BT           : no (size: 1)
DIS_CACHE            : no (size: 1)
BL

  17 │ K3_PART_RESERVE    : no (size: 1)
CHIP_CPU_FREQ_RATED  : YES (size: 1)
CHIP_CPU_FREQ_LOW    : no (size: 1)

  18 │[EFUSE BLK0 FLASH SPI][EFUSE BLK0 FLASH SPI]
WR_DIS               : 0x0080 (size: 16)
RD_DIS               : 0x1  (size: 4)
FLASH_CRYPT_CNT      : 1 (encryption ON) (size: 7)
FLASH_CRYPT_CONFIG   : 0xF (size: 4)
CLK8M_FREQ           : 54 (size: 8)
ADC_VREF             :

  19 │ 1 (size: 5)
VOL_LEVEL_HP_INV     : 0 (size: 2)
XPD_SDIO_REG         : no (size: 1)
XPD_SDIO_TIEH        : 1.8V (size: 1)
XPD_SDIO_FORCE       : no (size: 1)
SPI_PAD_CLK          : 0 (size: 5)
SPI_PAD_Q            : 0 (size: 5)
SPI_PAD_D   
          20 │: 0 (size: 5)
SPI_PAD_CS0          : 0 (size: 5)
SPI_PAD_HD           : 0 (size: 5)

  21 │[EFUSE BLK1  (FLASH ENCRYPTION)][EFUSE BLK1  (FLASH ENCRYPTION)]
scheme: NONE                 | empty: yes
  [0] 0x00000000
  [1] 0x00000000
  [2] 0x00000000
  [3] 0x00000000
  [4] 0x00000000
  [5] 0x00000000
  [6] 0x00000000
  [7] 0x00000000

  22 │[EFUSE BLK2  (SECURE BOOT)][EFUSE BLK2  (SECURE BOOT)]
scheme: NONE                 | empty: yes
  [0] 0x00000000
  [1] 0x00000000
  [2] 0x00000000
  [3] 0x00000000
  [4] 0x00000000
  [5] 0x00000000
  [6] 0x00000000
  [7] 0x00000000

  23 │[EFUSE BLK3  (USER)][EFUSE BLK3  (USER)]
scheme: NONE                 | empty: yes
  [0] 0x00000000
  [1] 0x00000000
  [2] 0x00000000
  [3] 0x00000000
  [4] 0x00000000
  [5] 0x00000000
  [6] 0x00000000
  [7] 0x00000000

  24 │[EFUSE KEY BLK1][EFUSE KEY BLK1]
purpose         : FLASH ENCRYPTION
purpose find    : found (BLK1)
unused          : no
read  protect   : YES
write protect   : YES
purpose wr dis  : YES

  25 │[EFUSE KEY BLK2][EFUSE KEY BLK2]
purpose         : SECURE BOOT
purpose find    : found (BLK2)
unused          : yes
read  protect   : no
write protect   : no
purpose wr dis  : YES

  26 │[EFUSE KEY BLK3][EFUSE KEY BLK3]
purpose         : USER
purpose find    : found (BLK3)
unused          : yes
read  protect   : no
write protect   : no
purpose wr dis  : YES

  27 │[EVENT LOOP][EVENT LOOP]

  28 │[MMU BLOCKS][MMU BLOCKS]
region 0:
Bus ID          Start          Free Head      End          Caps         Max Slot Size
0x1             0xd0000        0x160000       0x400000     0x9          0x2a0000  
mapped blocks:
ID   Vaddr Start   Vaddr End    Block Siz
  29 │ e   Caps   Paddr Start   Paddr End  
region 1:
Bus ID          Start          Free Head      End          Caps         Max Slot Size
0x8             0x1400000      0x1420000      0x1800000    0x1a         0x3e0000  
mapped blocks:
ID   Vaddr Start

  30 │ Vaddr End    Block Size   Caps   Paddr Start   Paddr End  
region 2:
Bus ID          Start          Free Head      End          Caps         Max Slot Size
0x10            0x1800000      0x1800000      0x1c00000    0x1e         0x400000  
mapped b

  31 │ locks:
ID   Vaddr Start   Vaddr End    Block Size   Caps   Paddr Start   Paddr End
*/