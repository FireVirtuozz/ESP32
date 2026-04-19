# Rust Station

This project is aiming to be a station control & monitoring for the RC car.

Using: 
- egui, egui_plot, eframe
- std : UdpSocket

Documentation:

**egui**

https://github.com/emilk/egui/wiki/3rd-party-egui-crates

https://crates.io/crates/egui/0.34.1

Librairies:

- `udp`: lib to communicate with the car through udp
- `gui`: lib using *egui* to plot data received from the car
- `monitor`, `parser`: lib to parse buffers from/to the car