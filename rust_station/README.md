# rust_station

PC side of the ESP32 projects: a Rust ground station that talks to the ESPs (UDP), controls the car, shows everything in a GUI, and runs odometry and SLAM.

<!-- TODO: add a screenshot of the egui GUI (car panel / SLAM map) here -->

## Folders

| Folder | What it is |
|---|---|
| [`station`](station) | **The main app**: ESP communication, control, egui GUI, odometry, SLAM, recorder, camera |
| [`train_ia`](train_ia) | Training of the driving model in a simulator (random room with obstacles), using the `burn` crate |
| [`ai_core`](ai_core) | <!-- TODO: one line, ex: shared model / inference code used by station and train_ia --> |
| [`tests_rust_benches_vec`](tests_rust_benches_vec) | <!-- TODO: one line, ex: small Rust tests and benchmarks I made while learning --> |

## station

- **Communication**: receives UDP frames from the ESPs and sends them commands
- **Control**: gamepad (PS4 / Xbox), commands sent every 30 ms. Pure Pursuit with waypoints is in progress.
- **GUI (egui)**: dashboard (Zigbee coordinator), sensor plots, OTA flashing, logs and dumps, motor curve tuning, camera view, car panel with estimated trajectory
- **Odometry**: pose from the IMU (BNO085) and the encoder (FC-33), with easy switching between frames
- **SLAM**: 360° scan accumulation from the LD500 lidar, log-odds grid mapping, ICP (point-to-line) and NDT
- **Recorder**: record sensor data and replay it later
- **AI** *(in progress)*: loads the weights of a model trained in `train_ia` and outputs decisions (turn, forward...)
<!-- TODO: check, my notes say "AI for control" is still a TODO -->
- **Config / OTA**: sends the camera and acceleration profile configs, runs an OTA server on request
- **Tailscale relay**: forwards the UDP data to Tailscale addresses (receive tasks only, no GUI)

Threads use `std`, with std / crossbeam channels, `arc-swap`, `Arc<RwLock>` and `Arc<Atomic>` to share data.

Note: it can be compiled for Android, but the interface is not ready yet.

## Run it

You need Rust stable ([rustup](https://rustup.rs)).

```bash
cargo run --release #run with release profile
```
You can canfigure the ports, ip used in [`station config`](station/config.toml).

## About this folder

I started it to learn Rust, following the [official book](https://doc.rust-lang.org/book/title-page.html), and it grew into the station of my RC car.