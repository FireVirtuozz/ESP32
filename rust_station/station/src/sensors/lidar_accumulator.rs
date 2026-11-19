//! Lidar accumulator
//! 
//! This module contains a thread that accumulates lidar data.
//! 
//! # Pipeline
//! Lidar D500 -> [uart] -> ESP32 -> Batch form -> [udp] -> Sensor Rust Station -> [channel] -> Accumulator Thread
//! 
//! # Thread
//! 
//! ## Inputs
//! - rx_packets: Lidar Batch
//! - shared_odo: Shared odometry data
//! 
//! ## Outputs
//! - lidar_diagnostics: Diagnostics of lidar data (for IHM)
//! - tx_slam: send lidar scan turn data to SLAM thread
//! - live_scan: Current turn scan (red dots on IHM)
//! 
//! ## Process
//! 1) Receives a batch
//! 2) Verify if this is plausible (in the right ranges)
//! 3) Update turn data, reset it if the delta with the last packet received is too much
//! 4) 
//! 
//! # TODO
//! - Unique channel for sensor -> accumulator (only for lidar packet)
//! 
//! 

use std::{
    f32::consts::FRAC_PI_2,
    fs::File,
    io::{BufWriter, Write},
    sync::{
        Arc, RwLock,
        atomic::{AtomicI32, AtomicU32, Ordering},
    },
    time::Instant,
};

use log::{debug, error, info};
use nalgebra::Point2;
use serde::{Deserialize, Serialize};

use crate::{
    error::AppError, odometry::{frames::{LIDAR, Pose}, odo::OdoState, slam::SlamEvent}, sensors::{TelemetryEnum, TelemetryPacket},
};




const BATCH_COUNT: usize = 20;
const PACKET_SIZE: usize = 44;
const BATCH_SIZE: usize = PACKET_SIZE * BATCH_COUNT;

const MIN_RANGE_M: f32 = 0.08;
const MAX_RANGE_M: f32 = 8.0;
const MIN_INTENSITY: u8 = 15;
const MAX_STEP_DEG: f32 = 3.0;
const MIN_COVER_DEG: f32 = 300.0;
const MIN_VALID_POINTS: usize = 100;
const TS_GAP_MS: u32 = 10;
const STREAM_BREAK_MS: u32 = 130;
const MAX_PLAUSIBLE_SPEED: f32 = 2.5;

const LIDAR_DIAG_PRINT: bool = false;
const LIDAR_DIAG_CSV: bool = false;

const LIDAR_TS_MOD: u32 = 30_000;

fn lidar_dt_ms(prev: u16, now: u16) -> u32 {
    let (p, n) = (prev as u32 % LIDAR_TS_MOD, now as u32 % LIDAR_TS_MOD);
    if n >= p { n - p } else { n + LIDAR_TS_MOD - p }
}

fn wrap_pm180(mut a: f32) -> f32 {
    if a < -180.0 {
        a += 360.0;
    } else if a > 180.0 {
        a -= 360.0;
    }
    a
}

/// `car_yaw` écran (0 = haut) → cap SLAM (0 = +x). À retirer quand l'odométrie stocke déjà ce cap.
fn slam_yaw(car_yaw: f32) -> f32 {
    car_yaw - FRAC_PI_2
}

/// Hit from Lidar to Car, using frames (isometries). LD500 outputs a clockwise angle, and is left-oriented.
fn hit_in_car(angle_deg: f32, distance_m: f32) -> Point2<f32> {
    let a = -angle_deg.to_radians();
    let (sin_a, cos_a) = a.sin_cos();
    let lidar_point = Point2::new(
        distance_m * cos_a,
        distance_m * sin_a,
    );
    LIDAR.iso() * lidar_point
}

fn sector(angle_deg: f32) -> usize {
    let car_deg = 90.0 - angle_deg;
    let sec = ((car_deg + 22.5).rem_euclid(360.0) / 45.0) as usize;
    sec.min(7)
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct RawPoint {
    pub distance_mm: u16,
    pub intensity: u8,
}

#[derive(Debug, Clone, Copy)]
struct Hit {
    angle_deg: f32,
    distance_m: f32,
    intensity: u8,
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct LidarD500Packet {
    pub radar_speed_deg_s: u16,
    pub start_angle_centideg: u16,
    pub end_angle_centideg: u16,
    pub timestamp_ms: u16,
    pub points: [RawPoint; 12],
}

impl LidarD500Packet {
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, AppError> {
        if bytes.len() != PACKET_SIZE {
            return Err(AppError::Other("Invalid lidar packet size".to_string()));
        }

        let radar_speed_deg_s = u16::from_le_bytes([bytes[0], bytes[1]]);
        let start_angle_centideg = u16::from_le_bytes([bytes[2], bytes[3]]);

        let mut points = [RawPoint { distance_mm: 0, intensity: 0 }; 12];
        for i in 0..12 {
            let offset = 4 + i * 3;
            points[i] = RawPoint {
                distance_mm: u16::from_le_bytes([bytes[offset], bytes[offset + 1]]),
                intensity: bytes[offset + 2],
            };
        }

        Ok(LidarD500Packet {
            radar_speed_deg_s,
            start_angle_centideg,
            end_angle_centideg: u16::from_le_bytes([bytes[40], bytes[41]]),
            timestamp_ms: u16::from_le_bytes([bytes[42], bytes[43]]),
            points,
        })
    }

    fn is_plausible(&self) -> bool {
        (self.timestamp_ms as u32) < LIDAR_TS_MOD
            && self.start_angle_centideg < 36_000
            && self.end_angle_centideg < 36_000
    }

    fn angle_step(&self) -> (f32, f32) {
        let start = self.start_angle_centideg as f32 / 100.0;
        let end = self.end_angle_centideg as f32 / 100.0;
        let diff = wrap_pm180(end - start);
        let step = if self.points.len() > 1 {
            diff / (self.points.len() - 1) as f32
        } else {
            0.0
        };
        (start, step)
    }

    fn hits(&self) -> impl Iterator<Item = Hit> + '_ {
        let (start, step) = self.angle_step();
        self.points.iter().enumerate().map(move |(i, raw)| Hit {
            angle_deg: (start + step * i as f32).rem_euclid(360.0),
            distance_m: raw.distance_mm as f32 / 1000.0,
            intensity: raw.intensity,
        })
    }
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize)]
pub struct LidarD500Batch {
    pub lidar_packets: [LidarD500Packet; BATCH_COUNT],
}

impl LidarD500Batch {
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, AppError> {
        if bytes.len() != BATCH_SIZE {
            debug!("[src] lot rejeté : {} octets au lieu de {}", bytes.len(), BATCH_SIZE);
            return Err(AppError::Other("Invalid lidar packet size".to_string()));
        }

        let mut lidar_packets = [LidarD500Packet::default(); BATCH_COUNT];
        for i in 0..BATCH_COUNT {
            let offset = i * PACKET_SIZE;
            lidar_packets[i] = LidarD500Packet::from_bytes(&bytes[offset..offset + PACKET_SIZE])?;
        }

        let batch = LidarD500Batch { lidar_packets };
        check_batch_continuity(&batch, "from_bytes");
        Ok(batch)
    }
}

#[derive(Default, Clone)]
pub struct LidarDiagnostics {
    pub radar_speed_deg_s: u16,
    pub timestamp_ms: u16,
    pub point_count: usize,
    pub scans_sent: u32,
    pub scans_dropped: u32,
    pub coverage_deg: f32,
}

/// Full diagnostic to csv file or in terminal
pub struct Diagnostic {
    file: Option<BufWriter<File>>,
    print: bool,
}

impl Diagnostic {
    pub fn open(name: &str, header: &str, csv: bool, print: bool) -> Self {
        let file = if csv {
            File::create(name).ok().map(|f| {
                let mut w = BufWriter::new(f);
                let _ = writeln!(w, "{header}");
                w
            })
        } else {
            None
        };
        Self { file, print }
    }

    pub fn row(&mut self, line: impl FnOnce() -> String) {
        if self.file.is_none() && !self.print {
            return;
        }
        let line = line();
        if self.print {
            info!("{line}");
        }
        if let Some(f) = self.file.as_mut() {
            let _ = writeln!(f, "{line}");
            let _ = f.flush();
        }
    }
}

/// Structure to represents a turn of lidar points (from 0° to 360°)
/// 
/// pts & fracs & covered_deg & last_angle are not debug data
struct Turn {
    pts: Vec<Point2<f32>>,
    fracs: Vec<f32>,
    n_raw: usize,
    rej_near: usize,
    rej_far: usize,
    rej_dim: usize,
    covered_deg: f32,
    max_step: f32,
    gaps: usize,
    ts_gaps: usize,
    sectors: [usize; 8],
    spd_min: u16,
    spd_max: u16,
    spd_sum: u64,
    spd_n: u32,
    last_angle: Option<f32>,
}

impl Turn {
    fn new() -> Self {
        Self {
            pts: Vec::with_capacity(520),
            fracs: Vec::with_capacity(520),
            n_raw: 0,
            rej_near: 0,
            rej_far: 0,
            rej_dim: 0,
            covered_deg: 0.0,
            max_step: 0.0,
            gaps: 0,
            ts_gaps: 0,
            sectors: [0; 8],
            spd_min: u16::MAX,
            spd_max: 0,
            spd_sum: 0,
            spd_n: 0,
            last_angle: None,
        }
    }

    /// Update speed data for debug
    fn note_speed(&mut self, speed_deg_s: u16) {
        self.spd_min = self.spd_min.min(speed_deg_s);
        self.spd_max = self.spd_max.max(speed_deg_s);
        self.spd_sum += speed_deg_s as u64;
        self.spd_n += 1;
    }

    /// Update gap data for debug
    fn note_time_gap(&mut self) {
        self.ts_gaps += 1;
    }

    /// Push a hit to the turn. Return Some if the turn is finished (angle wrapped)
    fn push(&mut self, hit: Hit) -> Option<Turn> {
        let done = self.take_if_wrapped(hit.angle_deg);
        self.n_raw += 1;
        self.keep(hit);
        self.last_angle = Some(hit.angle_deg);
        done
    }

    /// Take the turn if this is done, and reinit (replace) the object
    /// 
    /// Considers a wrap if angle + 180° < prev. 
    /// Example: old angle = 357°, new one = 3°; 3° + 180° < 357° -> wrap (new turn to consider)
    /// 
    /// The 180° is a "good" value for this case, but not optimal. 
    /// Ex: if a batch is not received: 190° + 180° <! 357°, so that is why there are timing tolerances between 2 batchs
    fn take_if_wrapped(&mut self, angle: f32) -> Option<Turn> {
        let Some(prev) = self.last_angle else {
            return None;
        };
        if angle + 180.0 < prev {
            return Some(std::mem::replace(self, Turn::new()));
        }
        let step = angle - prev;
        if step > 0.0 {
            self.max_step = self.max_step.max(step);
            if step <= MAX_STEP_DEG {
                self.covered_deg += step;
            }
        }
        if step > MAX_STEP_DEG || step < -1.0 {
            self.gaps += 1;
        }
        None
    }

    /// Hit conditions & debug depending on LD500, when hit is kept it is pushed to the turn's points 
    fn keep(&mut self, hit: Hit) {
        if hit.distance_m <= MIN_RANGE_M {
            self.rej_near += 1;
        } else if hit.distance_m >= MAX_RANGE_M {
            self.rej_far += 1;
        } else if hit.intensity <= MIN_INTENSITY {
            self.rej_dim += 1;
        } else {
            self.pts.push(hit_in_car(hit.angle_deg, hit.distance_m));
            self.fracs.push(hit.angle_deg / 360.0);
            self.sectors[sector(hit.angle_deg)] += 1;
        }
    }

    fn spd_avg(&self) -> u32 {
        if self.spd_n == 0 { 0 } else { (self.spd_sum / self.spd_n as u64) as u32 }
    }
}

static SRC_LAST_TS: AtomicI32 = AtomicI32::new(-1);
static SRC_LOST: AtomicU32 = AtomicU32::new(0);
static SRC_BATCHES: AtomicU32 = AtomicU32::new(0);

pub fn check_batch_continuity(batch: &LidarD500Batch, source: &str) {
    SRC_BATCHES.fetch_add(1, Ordering::Relaxed);
    let (Some(first), Some(last)) = (batch.lidar_packets.first(), batch.lidar_packets.last()) else {
        return;
    };
    let prev = SRC_LAST_TS.swap(last.timestamp_ms as i32, Ordering::Relaxed);
    if prev >= 0 {
        let dt = lidar_dt_ms(prev as u16, first.timestamp_ms);
        if dt > 10 && dt < 5000 {
            let lost = ((dt as f32 - 2.4) / 48.6).round().max(1.0) as u32;
            let total = SRC_LOST.fetch_add(lost, Ordering::Relaxed) + lost;
            debug!(
                "[src:{source}] trou de {dt} ms (~{lost} lot(s)) ; perdus {total} / reçus {}",
                SRC_BATCHES.load(Ordering::Relaxed)
            );
        }
    }
}

struct Rate {
    n: u32,
    since: Instant,
}

impl Rate {
    fn tick(&mut self) {
        self.n += 1;
        let dt = self.since.elapsed().as_secs_f32();
        if dt < 5.0 {
            return;
        }
        debug!(
            "[lidar] {:.1} lots/s reçus (attendu ~20.8 × replay_speed)",
            self.n as f32 / dt
        );
        self.n = 0;
        self.since = Instant::now();
    }
}

struct Bilan {
    n: usize,
    dropped: usize,
    gaps: usize,
    valid: f32,
    cover: f32,
    age: f32,
}

impl Bilan {
    fn add(&mut self, sent: bool, n_valid: usize, gaps: usize, cover: f32, age_ms: u32, breaks: u32) {
        self.n += 1;
        if !sent {
            self.dropped += 1;
        }
        if gaps > 0 {
            self.gaps += 1;
        }
        self.valid += n_valid as f32;
        self.cover += cover;
        self.age += age_ms as f32;
        if self.n < 100 {
            return;
        }
        let n = self.n as f32;
        debug!(
            "[lidar] BILAN {} tours : rejetés {:.0}% | avec trous {:.0}% | valides moy {:.0} | couverture moy {:.0}° | âge du wrap moy {:.0} ms | coupures flux {breaks}",
            self.n,
            100.0 * self.dropped as f32 / n,
            100.0 * self.gaps as f32 / n,
            self.valid / n,
            self.cover / n,
            self.age / n,
        );
        *self = Self::default();
    }
}

impl Default for Bilan {
    fn default() -> Self {
        Self { n: 0, dropped: 0, gaps: 0, valid: 0.0, cover: 0.0, age: 0.0 }
    }
}

struct Publisher {
    t0: Instant,
    diagnostic: Diagnostic,
    last_wrap_ts: Option<u16>,
    last_sent: Option<(Pose, u16)>,
    turns: usize,
    sent_idx: u32,
    dropped: u32,
    last_count: usize,
    last_cover: f32,
    bilan: Bilan,
}

impl Publisher {
    fn publish(
        &mut self,
        mut turn: Turn,
        wrap_ts: u16,
        batch_last_ts: u16,
        q_in: usize,
        q_slam: usize,
        breaks: u32,
        tx_slam: &crossbeam_channel::Sender<SlamEvent>,
        shared_odo: &RwLock<OdoState>,
        shared_live_scan: &RwLock<Vec<Point2<f32>>>,
    ) {
        self.turns += 1;
        let n_valid = turn.pts.len();
        let period_ms = self.last_wrap_ts.map(|p| lidar_dt_ms(p, wrap_ts)).unwrap_or(0);
        self.last_wrap_ts = Some(wrap_ts);
        let spd_avg = turn.spd_avg();
        let age_ms = lidar_dt_ms(wrap_ts, batch_last_ts);

        let (send, mut verdict) = verdict_of(&turn, q_in, period_ms, spd_avg);
        let mut delta_pose = Pose::default();
        let mut v_odo = 0.0;
        let sent_i = if send { self.sent_idx as i64 } else { -1 };

        if send {
            let car_pose = {
                let o = shared_odo.read().unwrap();
                o.car_pose
            };
            if let Some((last_pose, p_ts)) = self.last_sent {
                delta_pose = car_pose - last_pose;
                let since_s = lidar_dt_ms(p_ts, wrap_ts) as f32 / 1000.0;
                if since_s > 0.0 {
                    v_odo = delta_pose.get_translation_vector().norm() / since_s;
                }
                if v_odo > MAX_PLAUSIBLE_SPEED {
                    verdict.push_str("V_HIGH ");
                }
            }
            self.last_sent = Some((car_pose, wrap_ts));
            self.last_count = n_valid;
            self.last_cover = turn.covered_deg;

            let points = std::mem::take(&mut turn.pts);
            let fracs = std::mem::take(&mut turn.fracs);
            if let Ok(mut live) = shared_live_scan.write() {
                *live = points.clone();
            }
            let _ = tx_slam.send(SlamEvent::LidarScan {
                points,
                odom_pose: car_pose,
                fracs,
                coverage_deg: turn.covered_deg,
            });
            self.sent_idx += 1;
        } else {
            self.dropped += 1;
        }

        let sectors = turn.sectors.iter().map(|s| s.to_string()).collect::<Vec<_>>().join(",");
        let turns = self.turns;
        let wall_ms = self.t0.elapsed().as_millis();
        let odo_dx = delta_pose.get_translation_point().x;
        let odo_dy = delta_pose.get_translation_point().y;
        let odo_dyaw = delta_pose.get_rotation();
        self.diagnostic.row(|| format!(
            "{turns},{sent_i},{wall_ms},{period_ms},{spd_avg},{},{},{},{n_valid},{},{},{},{:.1},{:.2},{},{},{sectors},{odo_dx:.3},{odo_dy:.3},{odo_dyaw:.2},{v_odo:.2},{age_ms},{q_in},{q_slam},{}",
            turn.spd_min, turn.spd_max, turn.n_raw, turn.rej_near, turn.rej_far, turn.rej_dim,
            turn.covered_deg, turn.max_step, turn.gaps, turn.ts_gaps, verdict.trim_end(),
        ));
        self.bilan.add(send, n_valid, turn.gaps, turn.covered_deg, age_ms, breaks);
    }

    fn push_ui(&self, speed: u16, timestamp_ms: u16, shared: &RwLock<LidarDiagnostics>) {
        if let Ok(mut diag) = shared.write() {
            diag.radar_speed_deg_s = speed;
            diag.timestamp_ms = timestamp_ms;
            diag.point_count = self.last_count;
            diag.scans_sent = self.sent_idx;
            diag.scans_dropped = self.dropped;
            diag.coverage_deg = self.last_cover;
        }
    }
}

fn verdict_of(turn: &Turn, q_in: usize, period_ms: u32, spd_avg: u32) -> (bool, String) {
    let mut verdict = String::new();
    if turn.pts.len() < MIN_VALID_POINTS {
        verdict.push_str("DROP_FEW_VALID ");
    }
    if turn.covered_deg < MIN_COVER_DEG {
        verdict.push_str("DROP_LOW_COVER ");
    }
    let send = verdict.is_empty();
    if turn.gaps > 0 {
        verdict.push_str("GAPS ");
    }
    if spd_avg > 0 && period_ms > 0 {
        let expected = 360_000.0 / spd_avg as f32;
        if (period_ms as f32 - expected).abs() > 0.25 * expected {
            verdict.push_str("PERIOD? ");
        }
    }
    if q_in > 2 {
        verdict.push_str("LAG ");
    }
    (send, verdict)
}

pub fn spawn_lidar_accumulator_thread(
    rx_packets: crossbeam_channel::Receiver<TelemetryPacket>,
    tx_slam: crossbeam_channel::Sender<SlamEvent>,
    shared_odo: Arc<RwLock<OdoState>>,
    shared_live_scan: Arc<RwLock<Vec<Point2<f32>>>>,
    shared_lidar_diag: Arc<RwLock<LidarDiagnostics>>,
) {
    std::thread::spawn(move || {
        const HEADER: &str = "turn,sent_idx,wall_ms,period_ms,spd_avg,spd_min,spd_max,n_raw,n_valid,rej_near,rej_far,rej_dim,cover_deg,max_step_deg,gaps,ts_gaps,s_fwd,s_fl,s_l,s_rl,s_rear,s_rr,s_r,s_fr,odo_dx,odo_dy,odo_dyaw_deg,v_odo_ms,wrap_age_ms,q_in,q_slam,verdict";

        let mut publisher = Publisher {
            t0: Instant::now(),
            diagnostic: Diagnostic::open("lidar_diag.csv", HEADER, LIDAR_DIAG_CSV, LIDAR_DIAG_PRINT),
            last_wrap_ts: None,
            last_sent: None,
            turns: 0,
            sent_idx: 0,
            dropped: 0,
            last_count: 0,
            last_cover: 0.0,
            bilan: Bilan::default(),
        };
        let mut turn = Turn::new();
        let mut last_ts: Option<u16> = None;
        let mut breaks = 0u32;
        let mut bad_pkts = 0u32;
        let mut rate = Rate { n: 0, since: Instant::now() };

        while let Ok(packet) = rx_packets.recv() {
            let TelemetryPacket { packet: TelemetryEnum::LIDARD500(batch), .. } = packet else {
                continue;
            };
            rate.tick();

            let q_in = rx_packets.len();
            let mut finished = Vec::new();
            let mut speed_sum = 0u32;

            for lidar in &batch.lidar_packets {

                // Check if data received is in the range expected
                if !lidar.is_plausible() {
                    bad_pkts += 1;
                    if bad_pkts <= 10 {
                        debug!(
                            "[lidar] paquet invalide ignoré : ts={} start={} end={} speed={}",
                            lidar.timestamp_ms, lidar.start_angle_centideg,
                            lidar.end_angle_centideg, lidar.radar_speed_deg_s
                        );
                    }
                    continue;
                }

                // Reset turn or gap, then update it
                let ts = lidar.timestamp_ms;
                if let Some(prev) = last_ts {
                    let dt = lidar_dt_ms(prev, ts);
                    if dt > STREAM_BREAK_MS {
                        debug!("[lidar] flux interrompu ({dt} ms) : tour en cours abandonné");
                        turn = Turn::new();
                        breaks += 1;
                    } else if dt > TS_GAP_MS {
                        turn.note_time_gap();
                    }
                }
                last_ts = Some(ts);
                speed_sum += lidar.radar_speed_deg_s as u32;
                turn.note_speed(lidar.radar_speed_deg_s);

                // Loop for each point & add it to turn
                for hit in lidar.hits() {
                    if let Some(done) = turn.push(hit) {
                        finished.push((done, ts));
                    }
                }
            }

            // Publish debug data of finished turns
            let batch_last_ts = last_ts.unwrap_or(0);
            let q_slam = tx_slam.len();
            for (done, wrap_ts) in finished {
                publisher.publish(
                    done, wrap_ts, batch_last_ts, q_in, q_slam, breaks,
                    &tx_slam, &shared_odo, &shared_live_scan,
                );
            }

            let mean_speed = (speed_sum / batch.lidar_packets.len().max(1) as u32) as u16;
            publisher.push_ui(mean_speed, batch_last_ts, &shared_lidar_diag);
        }
    });
}