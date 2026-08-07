use crate::simulator::Wall;

#[derive(Clone)]
pub struct TrainingSnapshot {
    pub episode: u32,
    pub epsilon: f32,
    pub agent_x: f32,
    pub agent_y: f32,
    pub agent_theta: f32,
    pub walls: Vec<Wall>,
    pub last_reward: f32,
    pub episode_rewards: Vec<f32>,
    pub buffer_size: usize,
}

pub struct TrainingScreen {
    snapshot_rx: std::sync::mpsc::Receiver<TrainingSnapshot>,
    latest: Option<TrainingSnapshot>,
}

impl TrainingScreen {
    pub fn new(snapshot_rx: std::sync::mpsc::Receiver<TrainingSnapshot>) -> Self {
        Self { snapshot_rx, latest: None }
    }

    pub fn show(&mut self, ctx: &egui::Context) {
        while let Ok(snap) = self.snapshot_rx.try_recv() {
            self.latest = Some(snap);
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("RL Training Monitor");

            if let Some(snap) = &self.latest {
                ui.horizontal(|ui| {
                    ui.label(format!("Episode: {}", snap.episode));
                    ui.label(format!("Epsilon: {:.3}", snap.epsilon));
                    ui.label(format!("Buffer: {}", snap.buffer_size));
                });

                ui.columns(2, |cols| {
                cols[0].label("Simulation (above view)");
                let (response, painter) = cols[0].allocate_painter(egui::vec2(300.0, 300.0), egui::Sense::hover());
                let canvas_center = response.rect.center();
                let canvas_size = 300.0;

                if !snap.walls.is_empty() {
                    let (mut min_x, mut max_x, mut min_y, mut max_y) = (f32::MAX, f32::MIN, f32::MAX, f32::MIN);
                    for wall in &snap.walls {
                        min_x = min_x.min(wall.x1).min(wall.x2);
                        max_x = max_x.max(wall.x1).max(wall.x2);
                        min_y = min_y.min(wall.y1).min(wall.y2);
                        max_y = max_y.max(wall.y1).max(wall.y2);
                    }

                    let room_w = (max_x - min_x).max(0.1);
                    let room_h = (max_y - min_y).max(0.1);
                    let room_center_x = (min_x + max_x) / 2.0;
                    let room_center_y = (min_y + max_y) / 2.0;

                    let scale = (canvas_size * 0.9) / room_w.max(room_h);

                    let to_screen = |x: f32, y: f32| {
                        canvas_center + egui::vec2((x - room_center_x) * scale, (y - room_center_y) * scale)
                    };

                    for wall in &snap.walls {
                        painter.line_segment(
                            [to_screen(wall.x1, wall.y1), to_screen(wall.x2, wall.y2)],
                            egui::Stroke::new(2.0, egui::Color32::WHITE),
                        );
                    }

                    let agent_pos = to_screen(snap.agent_x, snap.agent_y);
                    let dir = egui::vec2(snap.agent_theta.cos(), snap.agent_theta.sin()) * 10.0;
                    painter.circle_filled(agent_pos, 5.0, egui::Color32::from_rgb(29, 158, 117));
                    painter.line_segment([agent_pos, agent_pos + dir], egui::Stroke::new(2.0, egui::Color32::YELLOW));
                }

                    cols[1].label("Reward/Episode");
                    let points: Vec<[f64; 2]> = snap.episode_rewards.iter().enumerate()
                        .map(|(i, r)| [i as f64, *r as f64]).collect();
                    egui_plot::Plot::new("reward_plot").height(280.0).show(&mut cols[1], |plot_ui| {
                        plot_ui.line(egui_plot::Line::new("reward", egui_plot::PlotPoints::from(points)));
                    });
                });
            } else {
                ui.label("Waiting for first snapshot..");
            }
        });

        ctx.request_repaint();
    }
}

impl eframe::App for TrainingScreen {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        self.show(ui.ctx());
    }
}