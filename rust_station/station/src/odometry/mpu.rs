use std::{sync::mpsc::{Receiver, Sender}, time::Instant};

use crate::{odometry::ProcessedTelemetry, sensors::{PacketImu, TelemetryEnum, TelemetryPacket}};

pub struct ProcessedMpu {
    pub timestamp: f64,
    pub ax_filt: f64,
    pub ay_filt: f64,
    pub vel_x: f64,
    pub vel_y: f64,
    pub gyro_z: f64,
}

pub fn spawn_integration_thread(
    rx_integ: crossbeam_channel::Receiver<TelemetryPacket>,
    tx_processed: Sender<ProcessedTelemetry>,
    start_instant: Instant,
) -> std::thread::JoinHandle<()> {
    std::thread::spawn(move || {
        // Paramètres du filtre et ZUPT
        const LOWPASS_ALPHA: f64 = 0.4;
        const ZUPT_ACCEL_THRESHOLD: f64 = 0.05;
        const ZUPT_GYRO_THRESHOLD: f64 = 3.0;
        const VEL_DECAY: f64 = 0.98;
        const ZUPT_DEBOUNCE_SAMPLES: u32 = 3;

        // État interne conservé entre les paquets
        let mut accel_filt_x: Option<f64> = None;
        let mut accel_filt_y: Option<f64> = None;
        let mut vel_x: f64 = 0.0;
        let mut vel_y: f64 = 0.0;
        let mut last_t: Option<f64> = None;
        let mut still_count_x = 0;
        let mut still_count_y = 0;

        while let Ok(packet) = rx_integ.recv() {
            if let TelemetryPacket { hd_info: _, packet: TelemetryEnum::MPU(pck) } = &packet {
                let t = start_instant.elapsed().as_secs_f64();
                let (ax_raw, ay_raw, _) = pck.get_accel_g();
                let (_, _, gyro_z) = pck.get_gyro_deg_s();

                // 1. Filtrage passe-bas
                let prev_x = accel_filt_x.unwrap_or(ax_raw);
                let ax_filt = LOWPASS_ALPHA * ax_raw + (1.0 - LOWPASS_ALPHA) * prev_x;
                accel_filt_x = Some(ax_filt);

                let prev_y = accel_filt_y.unwrap_or(ay_raw);
                let ay_filt = LOWPASS_ALPHA * ay_raw + (1.0 - LOWPASS_ALPHA) * prev_y;
                accel_filt_y = Some(ay_filt);

                // 2. Intégration et ZUPT
                if let Some(lt) = last_t {
                    let dt = (t - lt).max(0.0);

                    // X
                    vel_x += ax_filt * 9.81 * dt;
                    if ax_filt.abs() < ZUPT_ACCEL_THRESHOLD && gyro_z.abs() < ZUPT_GYRO_THRESHOLD {
                        still_count_x += 1;
                    } else {
                        still_count_x = 0;
                    }
                    if still_count_x >= ZUPT_DEBOUNCE_SAMPLES {
                        vel_x = 0.0;
                    } else {
                        vel_x *= VEL_DECAY;
                    }

                    // Y
                    vel_y += ay_filt * 9.81 * dt;
                    if ay_filt.abs() < ZUPT_ACCEL_THRESHOLD && gyro_z.abs() < ZUPT_GYRO_THRESHOLD {
                        still_count_y += 1;
                    } else {
                        still_count_y = 0;
                    }
                    if still_count_y >= ZUPT_DEBOUNCE_SAMPLES {
                        vel_y = 0.0;
                    } else {
                        vel_y *= VEL_DECAY;
                    }
                }
                last_t = Some(t);

                // 3. Envoi du point précalculé à egui
                let processed = ProcessedTelemetry::MPU(ProcessedMpu {
                    timestamp: t,
                    ax_filt,
                    ay_filt,
                    vel_x,
                    vel_y,
                    gyro_z,
                });

                if tx_processed.send(processed).is_err() {
                    break; // L'UI s'est fermée, fin du thread
                }
            }
        }
    })
}