use std::{
    collections::{HashMap, VecDeque}, f32::consts::{PI, TAU}, fs::File, io::BufWriter, num::NonZero, sync::{Arc, RwLock}, time::Instant,
};

use std::io::Write;

use kiddo::{ImmutableKdTree, dist::SquaredEuclidean};
use log::error;
use nalgebra::{Isometry2, Matrix2, Matrix3, Point2, Vector2, Vector3};

use crate::odometry::{frames::Pose, slam::{OccupancyGrid, SlamEvent, SlamState, update_grid_raycasting}};

const DIAG_PRINT_EVERY: usize = 0; // 0 = pas d'affichage console, N = une ligne sur N
const DIAG_CSV: bool = true;

// Ne pas confondre un déplacement lent avec un arrêt. L'ancien seuil (1,5 cm
// par scan et 1 degré) supprimait des incréments réels à vitesse lente.
const STILL_TRANSLATION_M: f32 = 0.002; // 2 mm par scan
const STILL_ROTATION_RAD: f32 = 0.15_f32.to_radians();

#[derive(Default, Clone, Copy)]
struct AlignDiag {
    iters: usize,
    used_first: usize,  // points appariés à la 1re itération (guess)
    used_last: usize,   // points appariés à la dernière
    score_first: f32,   // ICP : RMS point→droite (m) ; NDT : Mahalanobis moyenne (≈ 2 si bon)
    score_last: f32,
    lam_min: f32,       // valeurs propres du bloc translation de H (données seules)
    lam_max: f32,
    weak_dir_deg: f32,  // direction mal contrainte, repère monde
}

/// Get max, min intensities and the direction of the weakest intensity
/// 
/// Determined using eigenvalues of hessian matrix (info part 2x2)
/// Then direction is determined by searching the eigenvector related to the minimal eigenvalue
fn lam_dir(h: &Matrix3<f32>) -> (f32, f32, f32) {
    let (a, b, d) = (h[(0, 0)], h[(0, 1)], h[(1, 1)]);
    let tr = a + d;
    let disc = (tr * tr * 0.25 - (a * d - b * b)).max(0.0).sqrt();
    let (lmax, lmin) = (tr * 0.5 + disc, tr * 0.5 - disc);
    let weak = if b.abs() > 1e-9 { (lmin - a).atan2(b) }
               else if a < d { 0.0 } else { std::f32::consts::FRAC_PI_2 };
    (lmin, lmax, weak.to_degrees().rem_euclid(180.0))
}

/// Line fitting given a group of points
/// 
/// Computes the mean of points, and covariance for eigen vector (= direction of the line)
fn line_fit(pts: &[Point2<f32>]) -> Option<(Point2<f32>, Vector2<f32>)> {
    let n = pts.len() as f32;

    // computes the mean
    let c = pts.iter().fold(Vector2::zeros(), |s, p| s + p.coords) / n;

    // computes the covariance
    let (mut a, mut b, mut d) = (0.0_f32, 0.0_f32, 0.0_f32);
    for p in pts {
        let v = p.coords - c;
        a += v.x * v.x;
        b += v.x * v.y;
        d += v.y * v.y;
    }
    // eigen values
    let (tr, det) = (a + d, a * d - b * b);
    let disc = (tr * tr * 0.25 - det).max(0.0).sqrt();
    let (lmax, lmin) = (tr * 0.5 + disc, tr * 0.5 - disc);
    // detect if points are grouped (not a line)
    if lmax < 1e-5 || lmin > 0.05 * lmax {
        return None;
    }
    // Eigen vector
    let raw = if b.abs() > 1e-9 {
        Vector2::new(b, lmax - a)
    } else if a >= d {
        Vector2::new(1.0, 0.0)
    } else {
        Vector2::new(0.0, 1.0)
    };
    let dir = raw.normalize();
    Some((Point2::from(c), Vector2::new(-dir.y, dir.x)))
}

pub enum MatchAlgo {
    ScanToScan,
    /// Sous-carte locale, point-to-line. `keep_scans` tours, voxels de `voxel_m`.
    SubmapIcp { keep_scans: usize, voxel_m: f32 },
    /// Sous-carte locale, NDT. `ndt_voxel_m` est la taille des cellules gaussiennes.
    SubmapNdt { keep_scans: usize, voxel_m: f32, ndt_voxel_m: f32 },
}

impl MatchAlgo {
    fn keep_scans(&self) -> usize {
        match self {
            MatchAlgo::ScanToScan => 1,
            MatchAlgo::SubmapIcp { keep_scans, .. } | MatchAlgo::SubmapNdt { keep_scans, .. } => {
                (*keep_scans).max(1)
            }
        }
    }

    fn voxel_m(&self) -> f32 {
        match self {
            MatchAlgo::ScanToScan => 0.05,
            MatchAlgo::SubmapIcp { voxel_m, .. } | MatchAlgo::SubmapNdt { voxel_m, .. } => *voxel_m,
        }
    }
}

pub struct NdtCell {
    mean: Point2<f32>,
    inv_cov: Matrix2<f32>,
}

/// Spawn the SLAM thread, different algorithms can be used
pub fn spawn_compare_thread(
    rx_slam: crossbeam_channel::Receiver<SlamEvent>,
    shared_slam: Arc<RwLock<SlamState>>,
    algo: MatchAlgo,
) {
    std::thread::spawn(move || {
        let name = match &algo {
            MatchAlgo::ScanToScan => "scan2scan",
            MatchAlgo::SubmapIcp { .. } => "submap_icp",
            MatchAlgo::SubmapNdt { .. } => "submap_ndt",
        };
        const HEADER: &str = "idx,dt_ms,n_raw,inc_x,inc_y,inc_yaw_deg,sub_pts,used_first,used_last,used_ratio,score_first,score_last,lam_min,lam_max,weak_dir_deg,iters,corr_dt_cm,corr_dyaw_deg,accepted,off_dt_m,off_dyaw_deg,flags,off_x_m,off_y_m,corr_along_cm,corr_cross_cm";
        let mut csv = if DIAG_CSV {
            File::create(format!("slam_diag_{}.csv", name)).ok().map(BufWriter::new)
        } else { None };
        if let Some(f) = csv.as_mut() { let _ = writeln!(f, "{}", HEADER); }
        if DIAG_PRINT_EVERY > 0 { eprintln!("[{}] {}", name, HEADER); }

        let mut grid = OccupancyGrid::new(400, 400, 0.05);
        let mut pose = Pose::default();
        let mut last_odom: Option<Pose> = None;
        let mut submap: VecDeque<Vec<Point2<f32>>> = VecDeque::new();

        let mut idx = 0usize;
        let mut t_prev = Instant::now();
        let (mut ema_n, mut ema_dt) = (0.0_f32, 0.0_f32);
        let (mut s_n, mut s_rej) = (0usize, 0usize);
        let (mut s_score, mut s_dt, mut s_yaw) = (0.0_f32, 0.0_f32, 0.0_f32);

        while let Ok(event) = rx_slam.recv() {
            let SlamEvent::LidarScan { points, odom_pose , fracs, coverage_deg } = event;
            let t_now = Instant::now();
            let dt_ms = t_now.duration_since(t_prev).as_secs_f32() * 1000.0;
            t_prev = t_now;
            let n_raw = points.len();

            // Delta distance > 1m -> map reset
            if let Some(prev) = last_odom {
                if (odom_pose.get_translation_point() - prev.get_translation_point()).norm() > 1.0 {
                    error!("[{}] odom jump > 1 m : map reset", name);
                    grid = OccupancyGrid::new(400, 400, 0.05);
                    pose = Pose::default();
                    submap.clear();
                    last_odom = None;
                    publish(&shared_slam, &grid, &pose, &[]);
                }
            }

            // Increment between last SLAM pose and received odometry pose
            let inc = last_odom.map(|prev| odom_pose - prev);
            // Deskew points
            let scan = match inc {
                Some(d) => deskew_to_end(&points, &fracs, d),
                None => points,
            };

            let had_ref = !submap.is_empty();
            let sub_pts: usize = submap.iter().map(|s| s.len()).sum();
            let mut diag = AlignDiag::default();
            let mut accepted = true;
            let mut corr = Pose::default();

            let mut corr_w = Vector2::<f32>::zeros();
            // Estimate new pose
            let est = if !had_ref {
                odom_pose
            } else {
                let guess = match last_odom {
                    Some(prev) => pose * (odom_pose - prev), //increment pose
                    None => odom_pose,
                };


                let ref_world = flatten(&submap);
                let (matched, d) = match &algo {
                    MatchAlgo::ScanToScan | MatchAlgo::SubmapIcp { .. } => {
                        align_point_to_line(&ref_world, &scan, guess)
                    }
                    MatchAlgo::SubmapNdt { ndt_voxel_m, .. } => {
                        align_ndt(&build_ndt(&ref_world, *ndt_voxel_m), &scan, guess)
                    }
                };
                diag = d;
                corr = matched - guess;
                corr_w = matched.translation.vector - guess.translation.vector;

                // if the delta between the two is plausible, take the match (SLAM) pose
                // if not plausible, take the guess (odo) pose
                // cuz if too much distance between the two, maybe the SLAM exposed an outlier jump.
                accepted = plausible(guess, matched);
                if accepted { matched } else { guess }
            };

            // still detection to avoid updating raycasting & submap
            let still = match inc {
                Some(d) => d.get_translation_vector().norm() < STILL_TRANSLATION_M
                    && d.get_rotation().abs() < STILL_ROTATION_RAD,
                None => false,
            };

            // --- diagnostic ---
            let wk = diag.weak_dir_deg.to_radians();
            let w = Vector2::new(wk.cos(), wk.sin());
            let along = corr_w.dot(&w) * 100.0;                       // cm along corridor
            let cross = (corr_w - w * corr_w.dot(&w)).norm() * 100.0; // cm side

            let off = odom_pose.inverse() * est; // offset: SLAM - odom_pose
            let (inc_x, inc_y, inc_yaw) = match inc {
                Some(d) => (d.get_translation_point().x, d.get_translation_point().y, d.get_rotation().to_degrees()),
                None => (0.0, 0.0, 0.0),
            };
            let corr_dt_cm = corr.get_translation_vector().norm() * 100.0;
            let corr_dyaw = corr.get_rotation().to_degrees();
            let ratio = diag.used_last as f32 / scan.len().max(1) as f32;

            let mut flags = String::new();
            if still { flags += "STILL "; }
            if had_ref && !accepted { flags += "REJ "; }
            if ema_n > 0.0 && (n_raw as f32) > 1.6 * ema_n { flags += "MERGED? "; }
            if ema_n > 0.0 && (n_raw as f32) < 0.5 * ema_n { flags += "FEW "; }
            if ema_dt > 0.0 && dt_ms > 1.6 * ema_dt { flags += "GAP "; }
            if had_ref && ratio < 0.4 { flags += "LOWIN "; }
            if had_ref && diag.lam_max > 0.0 && diag.lam_min < 0.02 * diag.lam_max { flags += "DEGEN "; }
            ema_n = if ema_n == 0.0 { n_raw as f32 } else { 0.95 * ema_n + 0.05 * n_raw as f32 };
            ema_dt = if ema_dt == 0.0 { dt_ms } else { 0.95 * ema_dt + 0.05 * dt_ms };

            let row = format!(
                "{},{:.0},{},{:.3},{:.3},{:.2},{},{},{},{:.2},{:.4},{:.4},{:.1},{:.1},{:.0},{},{:.1},{:.2},{},{:.3},{:.2},{},{:.3},{:.3},{:.1},{:.1}",
                idx, dt_ms, n_raw, inc_x, inc_y, inc_yaw, sub_pts,
                diag.used_first, diag.used_last, ratio, diag.score_first, diag.score_last,
                diag.lam_min, diag.lam_max, diag.weak_dir_deg, diag.iters,
                corr_dt_cm, corr_dyaw, accepted as u8,
                off.get_translation_vector().norm(), off.get_rotation().to_degrees(), flags.trim_end(),
                off.get_translation_vector().x, off.get_translation_vector().y, along, cross
            );
            if DIAG_PRINT_EVERY > 0 && idx % DIAG_PRINT_EVERY == 0 { error!("[{}] {}", name, row); }
            if let Some(f) = csv.as_mut() { let _ = writeln!(f, "{}", row); }

            if had_ref {
                s_n += 1;
                if !accepted { s_rej += 1; }
                s_score += diag.score_last;
                s_dt += corr_dt_cm;
                s_yaw += corr_dyaw;
                if s_n >= 100 {
                    let n = s_n as f32;
                    error!(
                        "[{}] BILAN {} scans : rejets {:.0}% | score_last moyen {:.4} | corr moyenne {:.1} cm | biais yaw {:+.2}°",
                        name, s_n, 100.0 * s_rej as f32 / n, s_score / n, s_dt / n, s_yaw / n
                    );
                    (s_n, s_rej, s_score, s_dt, s_yaw) = (0, 0, 0.0, 0.0, 0.0);
                }
            }
            idx += 1;
            // --- end diagnostic ---

            // Update pose & grid & submap
            pose = est;
            if !still {
                update_grid_raycasting(&mut grid, pose, &scan);
                let world = voxel_down(scan.iter().map(|p| *pose * p).collect(), algo.voxel_m());
                submap.push_back(world);
                while submap.len() > algo.keep_scans() {
                    submap.pop_front();
                }
            }

            // shown points (current scan)
            let shown: Vec<Point2<f32>> = scan.iter().map(|p| *pose * p).collect();
            publish(&shared_slam, &grid, &pose, &shown);
            last_odom = Some(odom_pose);
        }
    });
}


/// Publish SLAM state
fn publish(shared: &Arc<RwLock<SlamState>>, grid: &OccupancyGrid, pose: &Isometry2<f32>, scan_world: &[Point2<f32>]) {
    // extract occupied grid points
    let mut grid_points = Vec::new();
    for y in 0..grid.height {
        for x in 0..grid.width {
            let idx = y * grid.width + x;
            if grid.cells[idx] > 1.5 { //1.5 log-odd value
                let wx = grid.origin.x + (x as f32 + 0.5) * grid.resolution;
                let wy = grid.origin.y + (y as f32 + 0.5) * grid.resolution;
                grid_points.push([wx as f64, wy as f64]);
            }
        }
    }
    // publish state
    if let Ok(mut slam) = shared.write() {
        slam.corrected_pose = (pose.translation.x, pose.translation.y, pose.rotation.angle());
        slam.map_points = scan_world.to_vec();
        slam.occupancy_grid = grid.clone();
        slam.cached_grid_points = grid_points;
    }
}

/// Deskew to end fixes the Lidar's motion distorsion
/// 
/// During a turn, the odometry changes (car moves)
/// So from the beginning to the end, we need to compensate the movement and apply it to the scan
/// 
/// Realized with a linear interp depending on the angle fraction of the turn
/// 
/// TODO: do it in thread accumulator, cuz we can't know the odom pose at the beginning of the turn
/// (currently we suppose previous slam pose, so deskew includes time between end turn and next begin turn)
fn deskew_to_end(points: &[Point2<f32>], fracs: &[f32], odom_delta: Pose) -> Vec<Point2<f32>> {
    if points.len() != fracs.len() || points.len() < 2 {
        return points.to_vec();
    }
    let start_in_end = odom_delta.inverse();
    let t = start_in_end.get_translation_vector();
    let yaw = start_in_end.get_rotation();
    points.iter().zip(fracs).map(|(p, f)| {
        let s = 1.0 - f.clamp(0.0, 1.0);
        let interp_pose = Pose::new(s * t.x, s * t.y, s * yaw);
        *interp_pose * p
    }).collect()
}

/// A delta between guess & match is considered plausible when < 10cm & < 4°
fn plausible(guess: Pose, est: Pose) -> bool {
    let d = est - guess;
    d.get_translation_vector().norm() < 0.10 && d.get_rotation().abs() < 4.0_f32.to_radians()
}

fn wrap_pi(a: f32) -> f32 {
    let mut d = a % TAU;
    if d > PI {
        d -= TAU;
    } else if d < -PI {
        d += TAU;
    }
    d
}

/// From window of scans to a fusionned contiguous vector
fn flatten(scans: &VecDeque<Vec<Point2<f32>>>) -> Vec<Point2<f32>> {
    scans.iter().flatten().copied().collect()
}

/// Voxel downsampling
/// 
/// Filters dense scans by retaining only the first point encountered per voxel grid cell.
fn voxel_down(points: Vec<Point2<f32>>, voxel: f32) -> Vec<Point2<f32>> {
    let mut seen = HashMap::<(i32, i32), Point2<f32>>::new();
    for p in points {
        let key = ((p.x / voxel).floor() as i32, (p.y / voxel).floor() as i32);
        seen.entry(key).or_insert(p);
    }
    seen.into_values().collect()
}

/// Align point to line
/// 
/// Using KD-Tree neighbour search & line fitting orthogonal distance optimization
fn align_point_to_line(refer: &[Point2<f32>], curr: &[Point2<f32>], guess: Pose) -> (Pose, AlignDiag) {
    let mut diag = AlignDiag::default();
    if refer.len() < 20 || curr.len() < 20 {
        return (guess, diag);
    }
    let arr: Vec<[f32; 2]> = refer.iter().map(|p| [p.x, p.y]).collect();

    // using KdTree: creates partitions of space
    let Ok(tree) = ImmutableKdTree::new_from_slice(&arr) else {
        return (guess, diag);
    };

    let k = NonZero::new(6usize).unwrap(); // get 6 closest neighbours
    let mut pose = guess;
    let mut first_done = false;
    // Stage1: accepting 45cm distance, doing 12 iterations; Stage2: 15cm, 8 iterations
    const STAGES: [(f32, usize); 2] = [(0.45 * 0.45, 12), (0.15 * 0.15, 8)];

    for &(max_d2, iters) in &STAGES {
        for _ in 0..iters {
            let mut h = Matrix3::<f32>::zeros();
            let mut g = Vector3::<f32>::zeros();
            let mut used = 0usize;
            let mut sse = 0.0_f32;
            let (st, ct) = pose.get_rotation().sin_cos();

            for p in curr {
                let q = *pose * p;
                // Search the 6 nearest points in current scan from previous scans
                let near = tree
                    .query(&[q.x, q.y])
                    .nearest_n::<SquaredEuclidean<f32>>(k)
                    .execute();
                if near.len() < 4 {
                    continue;
                }
                // Filter: closest point > tolerance || nearest points too spread out
                let dmin = near.iter().map(|n| n.distance).fold(f32::MAX, f32::min);
                let dmax = near.iter().map(|n| n.distance).fold(0.0, f32::max);
                if dmin > max_d2 || dmax > 0.3 * 0.3 {
                    continue;
                }
                // Computes a line fitting from neighbours
                let pts: Vec<Point2<f32>> = near.iter().map(|n| refer[n.item as usize]).collect();
                let Some((c, normal)) = line_fit(&pts) else { continue };
                // Computes orthogonal error (to the line before)
                let err = (q - c).dot(&normal);
                // Computes H & g
                let rot = Vector2::new(-p.x * st - p.y * ct, p.x * ct - p.y * st);
                let j = Vector3::new(normal.x, normal.y, rot.dot(&normal));
                h += j * j.transpose();
                g += j * err;
                sse += err * err;
                used += 1;
            }
            if used > 0 {
                let score = (sse / used as f32).sqrt();
                if !first_done {
                    first_done = true;
                    diag.used_first = used;
                    diag.score_first = score;
                    let (lmin, lmax, weak) = lam_dir(&h);
                    diag.lam_min = lmin;
                    diag.lam_max = lmax;
                    diag.weak_dir_deg = weak;
                }
                diag.used_last = used;
                diag.score_last = score;
            }
            diag.iters += 1;
            if used < 25 {
                break;
            }
            // Regularization (same case as NDT, but here no need for eigenvalues)
            // The higher the weight is, the higher the penalty would be when pose is too far away from guess
            let (wt, wr) = (10.0_f32, 2.0_f32);
            h[(0, 0)] += wt; //in x
            h[(1, 1)] += wt; //in y
            h[(2, 2)] += wr; //in rot
            g[0] += wt * (pose.translation.x - guess.translation.x);
            g[1] += wt * (pose.translation.y - guess.translation.y);
            g[2] += wr * wrap_pi(pose.get_rotation() - guess.get_rotation());

            // Gauss-Newton solving
            let Some(inv) = h.try_inverse() else { break };
            let step = inv * g;
            let t = pose.translation.vector - Vector2::new(step[0], step[1]);
            pose = Pose::new(t.x, t.y, pose.get_rotation() - step[2]);
            if step.norm() < 1e-4 {
                break;
            }
        }
    }
    (pose, diag)
}

/// Build NDT map, given a voxel (cell size)
fn build_ndt(points: &[Point2<f32>], voxel: f32) -> NdtMap {
    let mut buckets: HashMap<(i32, i32), Vec<Point2<f32>>> = HashMap::new();
    // assign each point to a cell, given a cell size (voxel)
    for p in points {
        let key = ((p.x / voxel).floor() as i32, (p.y / voxel).floor() as i32);
        buckets.entry(key).or_default().push(*p);
    }
    let mut ndt_map = NdtMap::new(voxel);
    for (key, pts) in buckets {
        if let Some(cell) = ndt_cell(&pts) {
            ndt_map.cells.insert(key, cell);
        }
    }
    ndt_map
}

/// Computes NDT data given points
/// 
/// More specifically, mean & inverse covariance (information) are computed
/// 
/// For covariance, apply a spectral flooring.
/// 
/// See in readme for more information.
fn ndt_cell(pts: &[Point2<f32>]) -> Option<NdtCell> {
    if pts.len() < 5 {
        return None;
    }

    // Computes mean
    let n = pts.len() as f32;
    let mut sx = 0.0f32;
    let mut sy = 0.0f32;
    for p in pts {
        sx += p.x;
        sy += p.y;
    }
    let mean = Point2::new(sx / n, sy / n);

    // Computes covariance
    let mut a = 0.0f32;
    let mut b = 0.0f32;
    let mut d = 0.0f32;
    for p in pts {
        let dx = p.x - mean.x;
        let dy = p.y - mean.y;
        a += dx * dx;
        b += dx * dy;
        d += dy * dy;
    }
    a /= n;
    b /= n;
    d /= n;

    // Spectral decomposition: change to eigenbasis to ensure matrix's inversibility
    let tr = a + d;
    let det = a * d - b * b;
    let disc = (tr * tr * 0.25 - det).max(0.0).sqrt();
    let l_max = (tr * 0.5 + disc).max(1e-4);
    let l_min = (tr * 0.5 - disc).max(l_max * 0.01).max(1e-4);

    let (ux, uy) = if b.abs() > 1e-8 {
        let vx = b;
        let vy = l_max - a;
        let s = (vx * vx + vy * vy).sqrt().max(1e-8);
        (vx / s, vy / s)
    } else if a >= d {
        (1.0, 0.0)
    } else {
        (0.0, 1.0)
    };
    let (px, py) = (-uy, ux); // Ortho eigen vector
    let c00 = l_max * ux * ux + l_min * px * px;
    let c01 = l_max * ux * uy + l_min * px * py;
    let c11 = l_max * uy * uy + l_min * py * py;
    let det_c = c00 * c11 - c01 * c01;
    if det_c < 1e-12 {
        return None;
    }
    let mut inv = Matrix2::<f32>::zeros();
    inv[(0, 0)] = c11 / det_c;
    inv[(0, 1)] = -c01 / det_c;
    inv[(1, 0)] = -c01 / det_c;
    inv[(1, 1)] = c00 / det_c;
    Some(NdtCell { mean, inv_cov: inv })
}

pub struct NdtMap {
    cells: HashMap<(i32, i32), NdtCell>,
    voxel_size: f32,
}

impl NdtMap {
    pub fn new(voxel_size: f32) -> Self {
        Self {
            cells: HashMap::new(),
            voxel_size,
        }
    }

    #[inline]
    pub fn world_to_index(&self, p: &Point2<f32>) -> (i32, i32) {
        (
            (p.x / self.voxel_size).floor() as i32,
            (p.y / self.voxel_size).floor() as i32,
        )
    }

    pub fn get(&self, idx: &(i32, i32)) -> Option<&NdtCell> {
        self.cells.get(idx)
    }
}


/// Align NDT
/// 
/// Find the optimized pose given a scan and the recorded NDT map.
/// 
/// See in readme for more information
fn align_ndt(
    ndt_map: &NdtMap,
    curr: &[Point2<f32>],
    guess: Pose,
) -> (Pose, AlignDiag) {
    let mut diag = AlignDiag::default();
    if ndt_map.cells.len() < 8 || curr.len() < 20 {
        return (guess, diag);
    }
    let mut pose = guess;
    let mut first_done = false;
    for iter in 0..14 {
        let maha_gate = if iter < 7 { 100.0_f32 } else { 9.0_f32 };
        let mut h = Matrix3::<f32>::zeros();
        let mut g = Vector3::<f32>::zeros();
        let mut used = 0usize;
        let mut sum_maha = 0.0_f32;
        let (st, ct) = pose.get_rotation().sin_cos();

        for p in curr {
            let pw = *pose * p;
            let (ix, iy) = ndt_map.world_to_index(&pw);

            // Search for best fit in NDT map (3x3 neighbours search)
            let mut best: Option<(&NdtCell, f32)> = None;
            for ox in -1..=1 {
                for oy in -1..=1 {
                    let Some(cell) = ndt_map.get(&(ix + ox, iy + oy)) else { continue };
                    let q = pw - cell.mean;
                    if q.norm_squared() > 0.8 * 0.8 { // point at 80cm from mean -> out
                        continue;
                    }
                    let maha = q.dot(&(cell.inv_cov * q));
                    if maha > maha_gate { // Outlier rejection
                        continue;
                    }
                    if best.map(|(_, m)| maha < m).unwrap_or(true) { // Update best
                        best = Some((cell, maha));
                    }
                }
            }
            let Some((cell, m)) = best else { continue };
            sum_maha += m;
            let q = pw - cell.mean;
            let info_q = cell.inv_cov * q; //Information on vector
            let jacob_rot = Vector2::new(-p.x * st - p.y * ct, p.x * ct - p.y * st);
            let w_rot = cell.inv_cov * jacob_rot; //Information on rotation

            g[0] += info_q[0];
            g[1] += info_q[1];
            g[2] += jacob_rot.dot(&info_q);
            h[(0, 0)] += cell.inv_cov[(0, 0)];
            h[(1, 1)] += cell.inv_cov[(1, 1)];
            h[(0, 1)] += cell.inv_cov[(0, 1)];
            h[(1, 0)] += cell.inv_cov[(1, 0)];
            h[(0, 2)] += w_rot[0];
            h[(2, 0)] += w_rot[0];
            h[(1, 2)] += w_rot[1];
            h[(2, 1)] += w_rot[1];
            h[(2, 2)] += jacob_rot.dot(&w_rot);
            used += 1;
        }
        if used > 0 {
            let score = sum_maha / used as f32;
            if !first_done {
                first_done = true;
                diag.used_first = used;
                diag.score_first = score;
                let (lmin, lmax, weak) = lam_dir(&h);
                diag.lam_min = lmin;
                diag.lam_max = lmax;
                diag.weak_dir_deg = weak;
            }
            diag.used_last = used;
            diag.score_last = score;
        }
        diag.iters += 1;
        if used < 25 {
            break;
        }

        let (_lmin, lmax, weak) = lam_dir(&h);
        let (sy, cx) = weak.to_radians().sin_cos();
        let (wx, wy) = (cx, sy); // weak axis direction
        let trans_diff = pose.get_translation_point() - guess.get_translation_point();
        //gets how much the car translated along the weak direction
        let along = Vector2::new(wx, wy).dot(&trans_diff);
        //damping force: more lmax is elsewhere, more penalty on the weak direction (more trust on odometry)
        let wt = lmax.max(1.0);

        h[(0, 0)] += wt * wx * wx;
        h[(1, 1)] += wt * wy * wy;
        h[(0, 1)] += wt * wx * wy;
        h[(1, 0)] += wt * wx * wy;
        g[0] += wt * wx * along;
        g[1] += wt * wy * along;
        h[(2, 2)] += wt;

        let d_odom = pose - guess;
        g[2] += wt * d_odom.get_rotation();

        let Some(inv) = h.try_inverse() else { break };
        let mut step = inv * g;

        //K-factor to accept 10cm & 3° max corrected pose
        let kf = (0.10 / Vector2::new(step[0], step[1]).norm().max(1e-9))
            .min(3.0_f32.to_radians() / step[2].abs().max(1e-9))
            .min(1.0);
        step *= kf;

        let t = pose.get_translation_vector() - Vector2::new(step[0], step[1]);
        pose = Pose::new(t.x, t.y, pose.get_rotation() - step[2]);
        if step.norm() < 1e-4 {
            break;
        }
    }
    (pose, diag)
}