use std::{fs::File, io::{BufRead, BufReader, BufWriter, Write}, sync::mpsc::{Receiver, Sender}, thread, time::Duration};
use crate::{config::AppConfig, sensors::TelemetryPacket};

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

pub fn replay_recording(tx: Sender<TelemetryPacket>, config_replay: AppConfig) {
    thread::spawn(move || {
        let frames = load_recording(&config_replay.replay_file);
        let mut last_ts: Option<f64> = None;

        for (ts, packet) in frames {
            if let Some(lt) = last_ts {
                let delta = ((ts - lt) / config_replay.replay_speed).max(0.0);
                thread::sleep(Duration::from_secs_f64(delta));
            }
            last_ts = Some(ts);
            if tx.send(packet).is_err() { break; } // fenêtre fermée = stop replay
        }
    });
}