use std::{fs::File, io::{BufRead, BufReader, BufWriter, Write}, sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::{Receiver, Sender}}, thread, time::Duration};
use crate::{config::AppConfig, sensors::{TelemetryEnum, TelemetryPacket}};

pub fn recorder_init(rx: Receiver<(TelemetryPacket, f64)>, config_recorder: AppConfig) -> thread::JoinHandle<()> {
    thread::spawn(move || {
        let file = File::create(&config_recorder.replay_file).expect("cannot create recording file");
        let mut writer = BufWriter::new(file);
        while let Ok((packet, ts)) = rx.recv() {
            if let Ok(line) = serde_json::to_string(&(ts, packet)) {
                let _ = writeln!(writer, "{}", line);
            }
        }
    })
}

pub fn load_recording(path: &str) -> Vec<(f64, TelemetryPacket)> {
    let file = File::open(path).expect("cannot open recording file");
    let reader = BufReader::new(file);
    reader.lines()
        .filter_map(|line| line.ok())
        .filter_map(|line| serde_json::from_str::<(f64, TelemetryPacket)>(&line).ok())
        .collect()
}

fn sensor_dt_ms(prev: u16, now: u16) -> u32 {
    if now >= prev { (now - prev) as u32 } else { now as u32 + 30_000 - prev as u32 }
}

pub fn analyze_recording(path: &str) {
    let reader = BufReader::new(File::open(path).expect("cannot open recording file"));
    let (mut n_lines, mut n_fail, mut n_fail_lidar) = (0usize, 0usize, 0usize);
    // (ts horloge PC, Some((ts capteur 1er paquet, ts capteur dernier paquet)) si lot lidar)
    let mut frames: Vec<(f64, Option<(u16, u16)>)> = Vec::new();

    for line in reader.lines().filter_map(|l| l.ok()) {
        n_lines += 1;
        match serde_json::from_str::<(f64, TelemetryPacket)>(&line) {
            Ok((ts, pkt)) => {
                let lidar = match &pkt.packet {
                    TelemetryEnum::LIDARD500(b) => Some((
                        b.lidar_packets[0].timestamp_ms,
                        b.lidar_packets[b.lidar_packets.len() - 1].timestamp_ms,
                    )),
                    _ => None,
                };
                frames.push((ts, lidar));
            }
            Err(e) => {
                n_fail += 1;
                if line.contains("LIDARD500") { n_fail_lidar += 1; }
                if n_fail <= 3 { eprintln!("[rec] ligne {} illisible : {}", n_lines, e); }
            }
        }
    }

    let t_total = match (frames.first(), frames.last()) { (Some(a), Some(b)) => b.0 - a.0, _ => 0.0 };
    let n_other = frames.iter().filter(|f| f.1.is_none()).count();
    let other_rate = n_other as f64 / t_total.max(1e-3); // paquets/s hors lidar

    let mut prev_l: Option<(usize, u16)> = None; // (index, ts capteur du dernier paquet du lot précédent)
    let (mut n_lidar, mut lost, mut n_gaps, mut max_ts) = (0usize, 0u32, 0usize, 0u16);
    let mut hist = [0usize; 11]; // trous classés par nombre de lots perdus d'affilée

    for (i, (ts, l)) in frames.iter().enumerate() {
        let Some((first, last)) = l else { continue };
        n_lidar += 1;
        max_ts = max_ts.max(*first).max(*last);
        if let Some((pi, p_last)) = prev_l {
            let dt = sensor_dt_ms(p_last, *first); // normal : 2-3 ms
            if dt > 10 {
                let k = ((dt as f32 - 2.4) / 48.6).round().max(1.0) as u32;
                lost += k;
                n_gaps += 1;
                hist[(k as usize).min(10)] += 1;
                let window = ts - frames[pi].0; // secondes, horloge PC
                let others = frames[pi + 1..i].iter().filter(|f| f.1.is_none()).count();
                if n_gaps <= 15 {
                    eprintln!(
                        "[rec] trou capteur {} ms (~{} lots) | PC : {:.0} ms entre les deux lots, autres paquets reçus {} (attendu ~{:.0})",
                        dt, k, window * 1000.0, others, other_rate * window
                    );
                }
            }
        }
        prev_l = Some((i, *last));
    }

    eprintln!(
        "[rec] {} lignes, {} illisibles ({} lidar) ; {} lots lidar lus, ~{} perdus ({:.0}%) en {} trous ; ts capteur max {} ; {:.1} s, {:.0} paquets/s hors lidar",
        n_lines, n_fail, n_fail_lidar, n_lidar, lost,
        100.0 * lost as f32 / (n_lidar as u32 + lost).max(1) as f32,
        n_gaps, max_ts, t_total, other_rate
    );
    eprintln!("[rec] trous par nombre de lots perdus (1 à 10+) : {:?}", &hist[1..]);
}

pub fn replay_recording(
    tx_sensors: crossbeam_channel::Sender<TelemetryPacket>, 
    config_replay: AppConfig, 
    sensors_connected: Arc<AtomicBool>,
    tx_integ: crossbeam_channel::Sender<TelemetryPacket>,
    tx_odo: crossbeam_channel::Sender<TelemetryPacket>,
    tx_lidar: crossbeam_channel::Sender<TelemetryPacket>,
) -> thread::JoinHandle<()> {
    thread::spawn(move || {

        analyze_recording(&config_replay.replay_file);
        let frames = load_recording(&config_replay.replay_file);
        let t_start = std::time::Instant::now();
        let ts0 = frames.first().map(|f| f.0).unwrap_or(0.0);
        for (ts, packet) in frames {
            sensors_connected.store(true, Ordering::Relaxed);
            let due = Duration::from_secs_f64(((ts - ts0) / config_replay.replay_speed).max(0.0));
            let elapsed = t_start.elapsed();
            if due > elapsed { thread::sleep(due - elapsed); }
            if tx_integ.send(packet.clone()).is_err() { break; }
            if tx_odo.send(packet.clone()).is_err() { break; }
            if tx_lidar.send(packet.clone()).is_err() { break; }
            if tx_sensors.send(packet).is_err() { break; }
        }
    })
}