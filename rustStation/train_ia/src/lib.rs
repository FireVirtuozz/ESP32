use std::sync::mpsc::Sender;

use ai_core::PolicyNet;
use burn::{Tensor, module::Module, optim::AdamConfig, record::CompactRecorder, tensor::backend::Backend};
use rand::RngExt;

use crate::{gui::TrainingSnapshot, simulator::Simulator, train::{ReplayBuffer, TrainBackend, Transition, train_step}};

pub mod train;
pub mod simulator;
pub mod gui;

pub fn main_loop(snapshot_tx: Sender<TrainingSnapshot>) {
    let device = Default::default();
    let mut model = PolicyNet::<TrainBackend>::new(&device);
    let mut optimizer = AdamConfig::new().init();
    let mut buffer = ReplayBuffer::new(10_000);
    let mut env = Simulator::new();
    let mut epsilon = 1.0;
    let mut episode_rewards_history: Vec<f32> = Vec::new();

    for episode in 0..10_000 {
        let mut state = env.reset();
        let mut episode_reward = 0.0;
        loop {
            let action = if rand::rng().random_range(0.0..1.0) < epsilon {
                rand::rng().random_range(0..3usize)
            } else {
                let tensor = state.to_tensor::<TrainBackend>(&device).unsqueeze::<2>();
                let q = model.forward(tensor).squeeze::<1>();
                q.argmax(0).into_scalar() as usize
            };

            let (next_state, reward, done) = env.step(action);
            episode_reward += reward;

            buffer.push(Transition { state: state.clone(), action, reward, next_state: next_state.clone(), done });
            state = next_state;

            if buffer.len() >= 64 {
                let batch = buffer.sample(64);
                model = train_step(model, &mut optimizer, &batch, &device, 0.99);
            }

            if env.steps % 20 == 0 {
                let recent_rewards: Vec<f32> = episode_rewards_history
                    .iter()
                    .rev()
                    .take(500) 
                    .rev()
                    .cloned()
                    .collect();

                let _ = snapshot_tx.send(TrainingSnapshot {
                    episode, epsilon,
                    agent_x: env.x, agent_y: env.y, agent_theta: env.theta,
                    walls: env.walls.clone(), last_reward: reward,
                    episode_rewards: recent_rewards, // capped
                    buffer_size: buffer.len(),
                });
            }

            if done { break; }
        }
        episode_rewards_history.push(episode_reward);
        epsilon = (epsilon * 0.995).max(0.05);
    }

    model.save_file("policy_trained", &CompactRecorder::new()).unwrap();
}