use burn::{Tensor, module::Module, nn::{Linear, LinearConfig, Relu}, tensor::backend::Backend};


#[derive(Clone, Debug)]
pub struct AgentState {
    pub hc_front: f32,
    pub hc_rear: f32,
    pub imu_ax: f32,
    pub imu_ay: f32,
    pub gyro_z: f32,
    pub velocity: f32,
}


impl AgentState {
    // Generic backend
    pub fn to_tensor<B: Backend>(&self, device: &B::Device) -> Tensor<B, 1> {
        Tensor::from_floats(
            [self.hc_front, self.hc_rear, self.imu_ax, self.imu_ay, self.gyro_z, self.velocity],
            device,
        )
    }
    
    pub fn to_vec(&self) -> Vec<f32> {
        vec![self.hc_front, self.hc_rear, self.imu_ax, self.imu_ay, self.gyro_z, self.velocity]
    }
}

#[derive(Module, Debug)]
pub struct PolicyNet<B: Backend> {
    fc1: Linear<B>,
    fc2: Linear<B>,
    fc3: Linear<B>,
    activation: Relu,
}

impl<B: Backend> PolicyNet<B> {
    pub fn new(device: &B::Device) -> Self {
        Self {
            fc1: LinearConfig::new(6, 32).init(device),  // 6 = AgentState size
            fc2: LinearConfig::new(32, 32).init(device),
            fc3: LinearConfig::new(32, 3).init(device),  // 3 actions: forward, turn left, turn right
            activation: Relu::new(),
        }
    }

    pub fn forward(&self, state: Tensor<B, 2>) -> Tensor<B, 2> {
        let x = self.activation.forward(self.fc1.forward(state));
        let x = self.activation.forward(self.fc2.forward(x));
        self.fc3.forward(x)
    }
}
