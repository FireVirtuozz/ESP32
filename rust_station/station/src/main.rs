use station::{config::AppConfig, error::AppError, run_app};

fn main() -> Result<(), AppError> {
    env_logger::init();

    run_app(eframe::NativeOptions::default())
}

#[cfg(test)]
mod tests {
    use nalgebra::Matrix3;

use super::*;

    #[test]
    fn it_works() {
        let mat = Matrix3::new(
            1.0, 2.0, 3.0, 
            4.0, 5.0, 6.0, 
            7.0, 8.0, 9.0
        );
        let mat2 = Matrix3::new(
            1.0, 4.0, 7.0,
            2.0, 5.0, 8.0,
            3.0, 6.0, 9.0,
        );
        println!("{}", mat);
        println!("{}", mat2);
    }
}