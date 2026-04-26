use std::{net::UdpSocket, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Sender}, thread};

use gilrs::{Gilrs, Event, Button, Axis};

use crate::error::AppError;

const UDP_FRAME_SIZE: usize = 9;

#[derive(Debug, Copy, Clone)]
pub struct ControllerPacket {
    pub left_x: i8,
    pub left_y: i8,
    pub right_x: i8,
    pub right_y: i8,
    pub left_trig: i8,
    pub right_trig: i8,
    pub buttons_mask: u8,
}

impl Default for ControllerPacket{
    fn default() -> Self {
        Self {
            left_x: 0,
            left_y: 0,
            right_x: 0,
            right_y: 0,
            left_trig: 0,
            right_trig: 0,
            buttons_mask: 0,
        }
    }
}

impl ControllerPacket {
    pub fn to_udp_frame(&self) -> [u8; UDP_FRAME_SIZE] {
        let mut frame: [u8; UDP_FRAME_SIZE] = [0; UDP_FRAME_SIZE];
        frame[0] = 0;
        frame[1] = 7;
        frame[2] = self.left_x as u8;
        frame[3] = self.left_y as u8;
        frame[4] = self.right_x as u8;
        frame[5] = self.right_y as u8;
        frame[6] = self.left_trig as u8;
        frame[7] = self.right_trig as u8;
        frame[8] = self.buttons_mask;
        frame
    }
}

pub fn init_controller(tx: Sender<ControllerPacket>, controller_connected: Arc<AtomicBool>) -> thread::JoinHandle<()> {
    let handle = thread::spawn(move || {
        if let Err(e) = controller_loop(tx, controller_connected) {
            eprintln!("UDP error: {:?}", e);
        }
    });
    handle
}

fn controller_loop(tx: Sender<ControllerPacket>, controller_connected: Arc<AtomicBool>) -> Result<(), AppError> {
    let mut gilrs = Gilrs::new().unwrap();
    let mut controller_pck = ControllerPacket::default();
    let socket = UdpSocket::bind("0.0.0.0:0")?;

    loop {
        while let Some(Event { id, event, .. }) = gilrs.next_event() {
            match event {
                gilrs::EventType::ButtonPressed(Button::South, _) => {
                    controller_pck.buttons_mask |= 0b00000001;
                },
                gilrs::EventType::ButtonPressed(Button::East, _) => {
                    controller_pck.buttons_mask |= 0b00000010;
                },
                gilrs::EventType::ButtonPressed(Button::West, _) => {
                    controller_pck.buttons_mask |= 0b00000100;
                },
                gilrs::EventType::ButtonPressed(Button::North, _) => {
                    controller_pck.buttons_mask |= 0b00001000;
                },
                gilrs::EventType::ButtonPressed(Button::DPadDown, _) => {
                    controller_pck.buttons_mask |= 0b00010000;
                },
                gilrs::EventType::ButtonPressed(Button::DPadLeft, _) => {
                    controller_pck.buttons_mask |= 0b00100000;
                },
                gilrs::EventType::ButtonPressed(Button::DPadRight, _) => {
                    controller_pck.buttons_mask |= 0b01000000;
                },
                gilrs::EventType::ButtonPressed(Button::DPadUp, _) => {
                    controller_pck.buttons_mask |= 0b10000000;
                },
                gilrs::EventType::ButtonReleased(Button::South, _) => {
                    controller_pck.buttons_mask &= !0b00000001;
                },
                gilrs::EventType::ButtonReleased(Button::East, _) => {
                    controller_pck.buttons_mask &= !0b00000010;
                },
                gilrs::EventType::ButtonReleased(Button::West, _) => {
                    controller_pck.buttons_mask &= !0b00000100;
                },
                gilrs::EventType::ButtonReleased(Button::North, _) => {
                    controller_pck.buttons_mask &= !0b00001000;
                },
                gilrs::EventType::ButtonReleased(Button::DPadDown, _) => {
                    controller_pck.buttons_mask &= !0b00010000;
                },
                gilrs::EventType::ButtonReleased(Button::DPadLeft, _) => {
                    controller_pck.buttons_mask &= !0b00100000;
                },
                gilrs::EventType::ButtonReleased(Button::DPadRight, _) => {
                    controller_pck.buttons_mask &= !0b01000000;
                },
                gilrs::EventType::ButtonReleased(Button::DPadUp, _) => {
                    controller_pck.buttons_mask &= !0b10000000;
                },
                gilrs::EventType::AxisChanged(Axis::LeftStickX, val, _) => {
                    let val = val * 100.0;
                    controller_pck.left_x = val as i8;
                },
                gilrs::EventType::AxisChanged(Axis::LeftStickY, val, _) => {
                    let val = val * -100.0;
                    controller_pck.left_y = val as i8;
                },
                gilrs::EventType::AxisChanged(Axis::RightStickX, val, _) => {
                    let val = val * 100.0;
                    controller_pck.right_x = val as i8;
                },
                gilrs::EventType::AxisChanged(Axis::RightStickY, val, _) => {
                    let val = val * -100.0;
                    controller_pck.right_y = val as i8;
                },
                gilrs::EventType::ButtonChanged(Button::LeftTrigger2, val, _) => {
                    let val = val * 100.0;
                    controller_pck.left_trig = val as i8;
                },
                gilrs::EventType::ButtonChanged(Button::RightTrigger2, val, _) => {
                    let val = val * 100.0;
                    controller_pck.right_trig = val as i8;
                },
                gilrs::EventType::Connected => {
                    controller_connected.store(true, Ordering::Relaxed);
                },
                gilrs::EventType::Disconnected => {
                    controller_connected.store(false, Ordering::Relaxed);
                },
                _ => {}
            }
        }

        socket.send_to(&controller_pck.to_udp_frame(), "192.168.4.1:3333")?;
        tx.send(controller_pck)?;
        thread::sleep(std::time::Duration::from_millis(30));
    }
}