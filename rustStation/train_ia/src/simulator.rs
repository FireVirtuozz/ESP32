use ai_core::AgentState;
use rand::RngExt;


#[derive(Clone, Copy)]
pub struct Wall {
    pub x1: f32, pub y1: f32,
    pub x2: f32, pub y2: f32,
}

pub struct Simulator {
    pub x: f32, pub y: f32, pub theta: f32, pub v: f32,
    pub walls: Vec<Wall>,
    pub visited_cells: std::collections::HashSet<(i32, i32)>,
    pub cell_size: f32,
    pub steps: u32,
}

impl Simulator {
    pub fn reset(&mut self) -> AgentState {
        let (walls, room_w, room_h) = generate_random_room();
        self.walls = walls;
        self.x = room_w / 2.0;
        self.y = room_h / 2.0;
        self.theta = rand::rng().random_range(0.0..std::f32::consts::TAU);
        self.v = 0.0;
        self.visited_cells.clear();
        self.steps = 0;
        self.compute_state()
    }

    pub fn step(&mut self, action: usize) -> (AgentState, f32, bool) {
        let dt = 0.05; // 50ms as HC period

        // Applies action to model
        let (target_v, omega) = match action {
            0 => (1.0, 0.0),   // forward
            1 => (0.5, -1.0),  // turn left
            2 => (0.5, 1.0),   // turn right
            _ => (0.0, 0.0),
        };
        self.v += (target_v - self.v) * 0.3; // approx motor curve
        self.theta += omega * dt;
        self.x += self.v * self.theta.cos() * dt;
        self.y += self.v * self.theta.sin() * dt;

        // Raycasting for HCs
        let hc_front = raycast(self.x, self.y, self.theta, &self.walls);
        let hc_rear = raycast(self.x, self.y, self.theta + std::f32::consts::PI, &self.walls);

        let collision = hc_front < 0.05 || hc_rear < 0.05; // 5cm safety

        // Récompense de couverture spatiale
        let cell = ((self.x / self.cell_size) as i32, (self.y / self.cell_size) as i32);
        let new_cell = self.visited_cells.insert(cell);
        let reward = if collision { -10.0 } else if new_cell { 1.0 } else { -0.01 }; // loss bonus for loss timings

        self.steps += 1;
        let done = collision || self.steps > 1000;

        (self.compute_state(), reward, done)
    }

    fn compute_state(&self) -> AgentState {
        AgentState {
            hc_front: raycast(self.x, self.y, self.theta, &self.walls),
            hc_rear: raycast(self.x, self.y, self.theta + std::f32::consts::PI, &self.walls),
            imu_ax: 0.0, imu_ay: 0.0, gyro_z: 0.0, // TODO: includes BNO085 reliable data
            velocity: self.v,
        }
    }

    pub fn new() -> Self {
        Self {
            x: 0.0, y: 0.0, theta: 0.0, v: 0.0,
            walls: Vec::new(),
            visited_cells: std::collections::HashSet::new(),
            cell_size: 0.2, // cell size for reward
            steps: 0,
        }
    }
}

fn ray_segment_intersect(px: f32, py: f32, dx: f32, dy: f32, wall: &Wall) -> Option<f32> {
    let (x1, y1, x2, y2) = (wall.x1, wall.y1, wall.x2, wall.y2);
    let (sx, sy) = (x2 - x1, y2 - y1);

    let denom = dx * sy - dy * sx;
    if denom.abs() < 1e-6 { return None; } // parallel ray to wall

    let t = ((x1 - px) * sy - (y1 - py) * sx) / denom;
    let u = ((x1 - px) * dy - (y1 - py) * dx) / denom;

    if t >= 0.0 && (0.0..=1.0).contains(&u) {
        Some(t) // distance along ray
    } else {
        None
    }
}

fn raycast(x: f32, y: f32, angle: f32, walls: &[Wall]) -> f32 {
    let max_dist = 4.0; // max range of HC (approx)
    let (dx, dy) = (angle.cos(), angle.sin());
    let mut closest = max_dist;

    for wall in walls {
        if let Some(dist) = ray_segment_intersect(x, y, dx, dy, wall) {
            if dist < closest { closest = dist; }
        }
    }
    closest
}

fn generate_random_room() -> (Vec<Wall>, f32, f32) {
    let mut rng = rand::rng();
    let room_w = rng.random_range(3.0..6.0);
    let room_h = rng.random_range(3.0..6.0);
    let (center_x, center_y) = (room_w / 2.0, room_h / 2.0);
    let spawn_clearance = 0.6; // security ray around car's spawn

    let mut walls = vec![
        Wall { x1: 0.0,    y1: 0.0,    x2: room_w, y2: 0.0 },
        Wall { x1: room_w, y1: 0.0,    x2: room_w, y2: room_h },
        Wall { x1: room_w, y1: room_h, x2: 0.0,    y2: room_h },
        Wall { x1: 0.0,    y1: room_h, x2: 0.0,    y2: 0.0 },
    ];

    let n_obstacles = rng.random_range(2..5);
    let mut placed: Vec<(f32, f32, f32, f32)> = Vec::new();

    for _ in 0..n_obstacles {
        let mut attempts = 0;
        loop {
            attempts += 1;
            if attempts > 20 { break; } // avoids infinite loop if room too tiny

            let ox = rng.random_range(1.0..(room_w - 1.0).max(1.1));
            let oy = rng.random_range(1.0..(room_h - 1.0).max(1.1));
            let ow = rng.random_range(0.3..0.8);
            let oh = rng.random_range(0.3..0.8);

            // spawn distance: rejects if too close
            let dist_to_center = ((ox + ow/2.0 - center_x).powi(2) + (oy + oh/2.0 - center_y).powi(2)).sqrt();
            if dist_to_center < spawn_clearance { continue; }

            // rejects if override obstacle already placed
            let overlaps = placed.iter().any(|(px, py, pw, ph)| {
                ox < px + pw && ox + ow > *px && oy < py + ph && oy + oh > *py
            });
            if overlaps { continue; }

            placed.push((ox, oy, ow, oh));
            walls.push(Wall { x1: ox,      y1: oy,      x2: ox + ow, y2: oy });
            walls.push(Wall { x1: ox + ow, y1: oy,      x2: ox + ow, y2: oy + oh });
            walls.push(Wall { x1: ox + ow, y1: oy + oh, x2: ox,      y2: oy + oh });
            walls.push(Wall { x1: ox,      y1: oy + oh, x2: ox,      y2: oy });
            break;
        }
    }

    (walls, room_w, room_h)
}