use eframe::egui;
use egui_plot::{Plot, Line, PlotPoints};

// Monitor
pub struct RcMonitor {
    voltage_history:     Vec<f64>,
    current_history:     Vec<f64>,
    distance_history:    Vec<f64>,
    temperature_history: Vec<f64>,
    tick: f64, // For simulation
}

impl RcMonitor {
    pub fn new() -> Self {
        RcMonitor {
            voltage_history:     Vec::new(),
            current_history:     Vec::new(),
            distance_history:    Vec::new(),
            temperature_history: Vec::new(),
            tick: 0.0,
        }
    }

    fn push(history: &mut Vec<f64>, val: f64) {
        history.push(val);
        if history.len() > 200 {
            history.remove(0);
        }
    }

    // Simulate data
    fn simulate(&mut self) {
        self.tick += 0.05;
        let t = self.tick;

        Self::push(&mut self.voltage_history,     7.4 + (t * 0.3).sin() * 0.3);
        Self::push(&mut self.current_history,     2.0 + (t * 0.7).sin() * 1.5);
        Self::push(&mut self.distance_history,    40.0 + (t * 0.5).sin() * 20.0);
        Self::push(&mut self.temperature_history, 23.0 + (t * 0.1).sin() * 2.0);
    }

    fn make_line(history: &Vec<f64>) -> Line {
        let points: PlotPoints = history
            .iter()
            .enumerate()
            .map(|(i, v)| [i as f64, *v])
            .collect();
        Line::new(points)
    }
}

impl eframe::App for RcMonitor {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.simulate();

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("RC Car Monitor");
            ui.separator();

            // Instant values
            ui.horizontal(|ui| {
                let v = self.voltage_history.last().copied().unwrap_or(0.0);
                let a = self.current_history.last().copied().unwrap_or(0.0);
                let d = self.distance_history.last().copied().unwrap_or(0.0);
                let t = self.temperature_history.last().copied().unwrap_or(0.0);

                ui.label(format!("🔋 {:.2} V", v));
                ui.separator();
                ui.label(format!("⚡ {:.2} A", a));
                ui.separator();
                ui.label(format!("📡 {:.0} cm", d));
                ui.separator();
                ui.label(format!("🌡 {:.1} °C", t));
            });

            ui.separator();

            // Voltage graph
            ui.label("Voltage (V)");
            Plot::new("voltage")
                .height(120.0)
                .include_y(6.0)
                .include_y(8.4)
                .show(ui, |plot_ui| {
                    plot_ui.line(Self::make_line(&self.voltage_history).name("V"));
                });

            // Current graph
            ui.label("Current (A)");
            Plot::new("current")
                .height(120.0)
                .include_y(0.0)
                .include_y(5.0)
                .show(ui, |plot_ui| {
                    plot_ui.line(Self::make_line(&self.current_history).name("A"));
                });

            // Distance graph
            ui.label("Distance (cm)");
            Plot::new("distance")
                .height(120.0)
                .include_y(0.0)
                .include_y(100.0)
                .show(ui, |plot_ui| {
                    plot_ui.line(Self::make_line(&self.distance_history).name("cm"));
                });

            // Graphe température
            ui.label("Temperature (°C)");
            Plot::new("temperature")
                .height(120.0)
                .include_y(0.0)
                .include_y(50.0)
                .show(ui, |plot_ui| {
                    plot_ui.line(Self::make_line(&self.temperature_history).name("°C"));
                });
        });

        ctx.request_repaint();
    }
}