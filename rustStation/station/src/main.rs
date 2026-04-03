mod gui;
mod monitor;
mod udp; 

fn main() -> Result<(), Box<dyn std::error::Error>> {
    /*
    eframe::run_native(
        "RC Car Monitor",
        eframe::NativeOptions {
            viewport: egui::ViewportBuilder::default()
                .with_inner_size([800.0, 700.0]),
            ..Default::default()
        },
        Box::new(|_| Box::new(gui::RcMonitor::new())),
    )
    */
    udp::udp_server_init()?;
    Ok(())
}