use train_ia::{gui::TrainingScreen, main_loop};

fn main() -> eframe::Result<()> {
    let (snapshot_tx, snapshot_rx) = std::sync::mpsc::channel();

    // training thread
    std::thread::spawn(move || {
        main_loop(snapshot_tx);
    });

    let options = eframe::NativeOptions {
        renderer: eframe::Renderer::Glow,
        ..Default::default()
    };
    eframe::run_native(
        "RL Training Monitor",
        options,
        Box::new(|_cc| Ok(Box::new(TrainingScreen::new(snapshot_rx)))),
    )
}

