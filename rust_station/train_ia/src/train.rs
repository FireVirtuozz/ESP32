use ai_core::{AgentState, PolicyNet};
use burn::Tensor;
use burn::backend::{Autodiff, NdArray};
use burn::optim::{AdamConfig, GradientsParams, Optimizer};
use burn::tensor::backend::{AutodiffBackend, Backend};
use rand::seq::IndexedRandom;

pub type TrainBackend = Autodiff<NdArray<f32>>;

#[derive(Clone)]
pub struct Transition {
    pub state: AgentState,
    pub action: usize,
    pub reward: f32,
    pub next_state: AgentState,
    pub done: bool,
}

pub struct ReplayBuffer {
    buffer: std::collections::VecDeque<Transition>,
    capacity: usize,
}

impl ReplayBuffer {
    pub fn new(capacity: usize) -> Self {
        Self { buffer: std::collections::VecDeque::with_capacity(capacity), capacity }
    }

    pub fn push(&mut self, t: Transition) {
        if self.buffer.len() >= self.capacity { self.buffer.pop_front(); }
        self.buffer.push_back(t);
    }

    pub fn sample(&self, batch_size: usize) -> Vec<Transition> {
        let mut rng = rand::rng();
        self.buffer.iter().cloned().collect::<Vec<_>>()
            .sample(&mut rng, batch_size.min(self.buffer.len()))
            .cloned()
            .collect()
    }

    pub fn len(&self) -> usize {
        self.buffer.len()
    }
    pub fn is_empty(&self) -> bool {
        self.buffer.is_empty()
    }
}

pub fn train_step<B: AutodiffBackend>(
    model: PolicyNet<B>,
    optimizer: &mut impl Optimizer<PolicyNet<B>, B>,
    batch: &[Transition],
    device: &B::Device,
    gamma: f32,
) -> PolicyNet<B> {
    let batch_size = batch.len();

    // Batch build
    let states: Vec<f32> = batch.iter().flat_map(|t| t.state.to_vec()).collect();
    let state_tensor = Tensor::<B, 2>::from_floats(
        burn::tensor::TensorData::new(states, [batch_size, 6]), device
    );

    let next_states: Vec<f32> = batch.iter().flat_map(|t| t.next_state.to_vec()).collect();
    let next_state_tensor = Tensor::<B, 2>::from_floats(
        burn::tensor::TensorData::new(next_states, [batch_size, 6]), device
    );

    // Q(s, a) predicts for action
    let q_values = model.forward(state_tensor.clone());

    // Target: reward + gamma * max_a' Q(s', a') (except end)
    let next_q_values = model.forward(next_state_tensor).detach(); // no gradient on target
    let max_next_q = next_q_values.max_dim(1);

    let rewards: Vec<f32> = batch.iter().map(|t| t.reward).collect();
    let dones: Vec<f32> = batch.iter().map(|t| if t.done { 0.0 } else { 1.0 }).collect();
    let reward_tensor = Tensor::<B, 1>::from_floats(rewards.as_slice(), device);
    let done_mask = Tensor::<B, 1>::from_floats(dones.as_slice(), device);

    let target = reward_tensor + max_next_q.squeeze::<1>() * done_mask * gamma;

    // Extracts Q(s, action_taken) within 3 Q-values of each line
    let actions: Vec<i64> = batch.iter().map(|t| t.action as i64).collect();
    let action_tensor = Tensor::<B, 1, burn::tensor::Int>::from_ints(actions.as_slice(), device);
    let predicted = q_values.gather(1, action_tensor.unsqueeze_dim(1)).squeeze::<1>();

    // Loss MSE + backward
    let loss = (predicted - target).powf_scalar(2.0).mean();
    let grads = loss.backward();
    let grads_params = GradientsParams::from_grads(grads, &model);

    optimizer.step(1e-3, model, grads_params)
}