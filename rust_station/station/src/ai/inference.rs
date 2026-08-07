use std::sync::mpsc::{Receiver, Sender};

use ai_core::{AgentState, PolicyNet};
use burn::{Tensor, backend::NdArray, module::Module, nn::{Linear, LinearConfig, Relu}, record::{CompactRecorder, Recorder}, tensor::{Device, backend::{Backend, BackendTypes}}};

pub type InferenceBackend = burn::backend::NdArray<f32>;

fn action_to_command(action_idx: usize) -> (i8, i8) {
    match action_idx {
        0 => (100, 0),    // forward
        1 => (100, -100), // turn left
        2 => (100, 100),  // turn right
        _ => (0, 0),  
    }
}

pub fn inference_thread(
    state_rx: Receiver<AgentState>,
    action_tx: Sender<(i8, i8)>,
    model: PolicyNet<InferenceBackend>,
    device: <InferenceBackend as BackendTypes>::Device,
) {
    std::thread::spawn(move || {
        while let Ok(state) = state_rx.recv() {
            let tensor = state.to_tensor::<InferenceBackend>(&device).unsqueeze::<2>();
            let q_values = model.forward(tensor).squeeze::<1>();

            let action_idx: usize = q_values.argmax(0).into_scalar() as usize;
            let (motor, angle) = action_to_command(action_idx);

            let _ = action_tx.send((motor, angle));
        }
    });
}

pub fn load_model(path: &str, device: &<InferenceBackend as BackendTypes>::Device) -> PolicyNet<InferenceBackend> {
    let model = PolicyNet::new(device);
    let record = CompactRecorder::new()
        .load(path.into(), device)
        .expect("failed to load model weights");
    model.load_record(record)
}