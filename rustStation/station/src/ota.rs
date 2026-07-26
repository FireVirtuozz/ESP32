use std::{fs::File, process::Command, sync::{Arc, Mutex}};
use tiny_http::{Response, Server};

#[derive(Clone, Default)]
pub struct OtaServerStatus {
    pub running: bool,
    pub last_client_ip: Option<String>,
    pub bytes_served: u64,
}

pub fn fetch_latest_bin() -> Result<(), Box<dyn std::error::Error>> {
    println!("Récupération du dernier .bin depuis Xubuntu...");

    let status = Command::new("scp")
        .arg("azoxvirtuozz@192.168.1.48:~/projects/ESP32/esp_project/build/esp_project.bin")
        .arg("./firmware.bin")
        .status()?;

    if status.success() {
        println!("Fichier binaire récupéré avec succès !");
    } else {
        eprintln!("Échec du transfert SCP.");
    }

    Ok(())
}

pub fn serve_firmware(bind_addr: &str, firmware_path: String, status: Arc<Mutex<OtaServerStatus>>) {
    let server = Server::http(bind_addr).unwrap();
    status.lock().unwrap().running = true;

    std::thread::spawn(move || {
        for request in server.incoming_requests() {
            if request.url() == "/firmware.bin" {
                if let Ok(file) = File::open(&firmware_path) {
                    let len = file.metadata().map(|m| m.len()).unwrap_or(0);
                    let mut s = status.lock().unwrap();
                    s.last_client_ip = Some(format!("{:?}", request.remote_addr()));
                    s.bytes_served += len;
                    drop(s);
                    let _ = request.respond(Response::from_file(file));
                } else {
                    let _ = request.respond(Response::empty(404));
                }
            } else {
                let _ = request.respond(Response::empty(404));
            }
        }
    });
}