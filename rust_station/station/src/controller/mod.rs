use std::{net::UdpSocket, println, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Sender}, thread, time::Instant};

use crossbeam_channel::Sender as CrossbeamSender;
use gilrs::{Gilrs, Event, Button, Axis};
use log::error;

use crate::{control::{Bounded, ControlType, DriveCommand, DriveMode, DriveModeCommand, MiscellaneousCommand, ModeState, SafetyCommand}, error::AppError};

const UDP_FRAME_SIZE: usize = 13;

#[derive(Debug, Copy, Clone, Default)]
pub struct ControllerPacket {
    pub left_x: f32,
    pub left_y: f32,
    pub right_x: f32,
    pub right_y: f32,
    pub left_trig: f32,
    pub right_trig: f32,
    pub right_arrow: bool,
    pub left_arrow: bool,
    pub up_arrow: bool,
    pub down_arrow: bool,
    pub right_button: bool,
    pub left_button: bool,
    pub up_button: bool,
    pub down_button: bool,
}

impl ControllerPacket {
    pub fn to_drive_command(&self, mode_state: DriveMode) -> DriveCommand {
        let throttle = match (self.right_trig > 0.01, self.left_trig > 0.01) {
            (true, true) => 0i16,
            (true, false) => (self.right_trig * 1.20 * mode_state.mode_factor() * 1000.0) as i16,
            (false, true) => (self.left_trig * mode_state.mode_factor() * -1000.0) as i16,
            (false, false) => 0i16,
        };
        let steering = (self.left_x * 1000.0) as i16;
        DriveCommand { throttle: Bounded::new(throttle), steering: Bounded::new(steering) }
    }
}

pub fn init_controller(
    tx: Sender<ControllerPacket>,
    tx_control: CrossbeamSender<ControlType>, 
    tx_safety: CrossbeamSender<SafetyCommand>, 
    tx_misc: CrossbeamSender<MiscellaneousCommand>, 
    controller_connected: Arc<AtomicBool>, 
    active_mode: Arc<ModeState>,
) -> thread::JoinHandle<()> {
    let handle = thread::spawn(move || {
        if let Err(e) = controller_loop(tx, tx_control, tx_safety, 
            tx_misc, controller_connected, active_mode) {
            error!("UDP error ctrl: {:?}", e);
        }
    });
    handle
}

fn controller_loop(
    tx: Sender<ControllerPacket>,
    tx_control: CrossbeamSender<ControlType>,
    tx_safety: CrossbeamSender<SafetyCommand>, 
    tx_misc: CrossbeamSender<MiscellaneousCommand>, 
    controller_connected: Arc<AtomicBool>, 
    active_mode: Arc<ModeState>,
) -> Result<(), AppError> {
    let mut gilrs = Gilrs::new().unwrap();
    let mut controller_pck = ControllerPacket::default();

    for (_id, gamepad) in gilrs.gamepads() {
        if gamepad.is_connected() {
            controller_connected.store(true, Ordering::Relaxed);
            break;
        }
    }

    let mut last_up_arrow = false;
    let mut last_down_arrow = false;
    let mut last_right_arrow = false;
    let mut last_left_arrow = false;

    let mut last_left_button = false;
    let mut last_down_button = false;

    let mut hc_block_activated = false;
    let mut forced_stop = false;
    
    let mut misc_cmd = MiscellaneousCommand::default();

    loop {

        if !controller_connected.load(Ordering::Relaxed) {
            // On consomme les événements de déconnexion/connexion en arrière-plan sans bloquer
            while let Some(Event { event, .. }) = gilrs.next_event() {
                match event {
                    gilrs::EventType::Connected => {
                        controller_connected.store(true, Ordering::Relaxed);
                    }
                _ => {} // tout le reste, ignoré tant que déconnecté
                }
            }
            
            thread::sleep(std::time::Duration::from_millis(500));
            continue;
        }
        
        while let Some(Event { id, event, .. }) = gilrs.next_event() {
            match event {
                gilrs::EventType::ButtonPressed(Button::South, _) => {
                    controller_pck.down_button = true;
                },
                gilrs::EventType::ButtonPressed(Button::East, _) => {
                    controller_pck.right_button = true;
                },
                gilrs::EventType::ButtonPressed(Button::West, _) => {
                    controller_pck.left_button = true;
                },
                gilrs::EventType::ButtonPressed(Button::North, _) => {
                    controller_pck.up_button = true;
                },
                gilrs::EventType::ButtonPressed(Button::DPadDown, _) => {
                    controller_pck.down_arrow = true;
                },
                gilrs::EventType::ButtonPressed(Button::DPadLeft, _) => {
                    controller_pck.left_arrow = true;
                },
                gilrs::EventType::ButtonPressed(Button::DPadRight, _) => {
                    controller_pck.right_arrow = true;
                },
                gilrs::EventType::ButtonPressed(Button::DPadUp, _) => {
                    controller_pck.up_arrow = true;
                },
                gilrs::EventType::ButtonReleased(Button::South, _) => {
                    controller_pck.down_button = false;
                },
                gilrs::EventType::ButtonReleased(Button::East, _) => {
                    controller_pck.right_button = false;
                },
                gilrs::EventType::ButtonReleased(Button::West, _) => {
                    controller_pck.left_button = false;
                },
                gilrs::EventType::ButtonReleased(Button::North, _) => {
                    controller_pck.up_button = false;
                },
                gilrs::EventType::ButtonReleased(Button::DPadDown, _) => {
                    controller_pck.down_arrow = false;
                },
                gilrs::EventType::ButtonReleased(Button::DPadLeft, _) => {
                    controller_pck.left_arrow = false;
                },
                gilrs::EventType::ButtonReleased(Button::DPadRight, _) => {
                    controller_pck.right_arrow = false;
                },
                gilrs::EventType::ButtonReleased(Button::DPadUp, _) => {
                    controller_pck.up_arrow = false;
                },
                gilrs::EventType::AxisChanged(Axis::LeftStickX, val, _) => {
                    controller_pck.left_x = val;
                },
                gilrs::EventType::AxisChanged(Axis::LeftStickY, val, _) => {
                    controller_pck.left_y = -val;
                },
                gilrs::EventType::AxisChanged(Axis::RightStickX, val, _) => {
                    controller_pck.right_x = val;
                },
                gilrs::EventType::AxisChanged(Axis::RightStickY, val, _) => {
                    controller_pck.right_y = -val;
                },
                gilrs::EventType::ButtonChanged(Button::LeftTrigger2, val, _) => {
                    controller_pck.left_trig = val;
                },
                gilrs::EventType::ButtonChanged(Button::RightTrigger2, val, _) => {
                    controller_pck.right_trig = val;
                },
                gilrs::EventType::Connected => {
                    controller_connected.store(true, Ordering::Relaxed);
                },
                gilrs::EventType::Disconnected => {
                    controller_connected.store(false, Ordering::Relaxed);
                    controller_pck = ControllerPacket::default();
                    forced_stop = true;
                    hc_block_activated = true;
                    if let Err(e) = tx_safety.send(SafetyCommand { hc_block: hc_block_activated, brake_stop: forced_stop }) {
                        error!("failed to send safety command on disconnect {e}");
                    }
                },
                _ => {}
            }
        }

        match (controller_pck.right_arrow, last_right_arrow) {
            (true, false) => active_mode.increment(),//increase mode
            _ => {},
        }

        match (controller_pck.left_arrow, last_left_arrow) {
            (true, false) => active_mode.decrement(),//decrease mode
            _ => {},
        }

        let mut safety_to_send = false;
        let mut misc_to_send = false;

        match (controller_pck.down_arrow, last_down_arrow) {
            (true, false) => {
                hc_block_activated = !hc_block_activated;
                safety_to_send = true;
            },//hc_block toggle
            _ => {},
        }

        match (controller_pck.up_arrow, last_up_arrow) {
            (true, false) => {
                forced_stop = true;
                safety_to_send = true;
            },//brake_stop start
            (false, true) => {
                forced_stop = false;
                safety_to_send = true;
            },//brake_stop end
            _ => {},
        }

        match (controller_pck.left_button, last_left_button) {
            (true, false) => {
                misc_cmd.horn = true;
                misc_to_send = true;
            },//brake_stop start
            (false, true) => {
                misc_cmd.horn = false;
                misc_to_send = true;
            },//brake_stop end
            _ => {},
        }

        match (controller_pck.down_button, last_down_button) {
            (true, false) => {
                match misc_cmd.drive_mode {
                    DriveModeCommand::Interp => misc_cmd.drive_mode = DriveModeCommand::Speed,
                    DriveModeCommand::Speed => misc_cmd.drive_mode = DriveModeCommand::Interp,
                    DriveModeCommand::Raw => {},
                }
                misc_to_send = true;
            },
            _ => {},
        }


        if safety_to_send {
            if let Err(e) = tx_safety
            .send(SafetyCommand { hc_block: hc_block_activated, brake_stop: forced_stop }) {
                error!("failed to send safety command {e}");
            }
        }

        if misc_to_send {
            if let Err(e) = tx_misc
            .send(misc_cmd.clone()) {
                error!("failed to send safety command {e}");
            }
        }

        last_right_arrow = controller_pck.right_arrow;
        last_left_arrow = controller_pck.left_arrow;
        last_down_arrow = controller_pck.down_arrow;
        last_up_arrow = controller_pck.up_arrow;
        last_left_button = controller_pck.left_button;
        last_down_button = controller_pck.down_button;

        if let Err(e) = tx_control.
        send(ControlType::Gamepad(controller_pck.to_drive_command(active_mode.get()))) {
            error!("error sending control {e}");
        }
        if let Err(e) = tx.send(controller_pck) {
            error!("error sending controller {e}");
        };
        thread::sleep(std::time::Duration::from_millis(30));
    }
}