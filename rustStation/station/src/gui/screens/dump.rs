use std::collections::BTreeSet;

use egui::{Color32, FontId, RichText, ScrollArea};

use crate::gui::ScreensTypes;

pub struct DumpEntry {
    pub library: String,
    pub name: String,
    pub esp_id: u8,
    pub timestamp: u32,
    pub content: String,
}

impl DumpEntry {
    /// Parse le gros Vec<u8> réassemblé pour créer la structure
    pub fn parse_from_buf(bytes: &[u8]) -> Option<Self> {
        // Sécurité minimale : esp_id(1) + timestamp(4) + lib_len(1) + name_len(1) = 7 octets minimum
        if bytes.len() < 7 { return None; } 

        let mut cursor = 0;

        // 1. ESP ID
        let esp_id = bytes[cursor];
        cursor += 1;
        
        // 2. Timestamp (Little Endian)
        let timestamp_bytes: [u8; 4] = bytes[cursor..cursor+4].try_into().ok()?;
        let timestamp = u32::from_le_bytes(timestamp_bytes);
        cursor += 4;

        // 3. Extraction de la Library
        if cursor >= bytes.len() { return None; }
        let lib_len = bytes[cursor] as usize;
        cursor += 1;

        let lib_end = cursor + lib_len;
        if lib_end > bytes.len() { return None; }
        let library = String::from_utf8_lossy(&bytes[cursor..lib_end]).into_owned();
        cursor = lib_end; // Le curseur avance à la fin de la chaîne library

        // 4. Extraction du Name
        if cursor >= bytes.len() { return None; }
        let name_len = bytes[cursor] as usize;
        cursor += 1;

        let name_end = cursor + name_len;
        if name_end > bytes.len() { return None; }
        let name = String::from_utf8_lossy(&bytes[cursor..name_end]).into_owned();
        cursor = name_end; // Le curseur avance à la fin de la chaîne name

        // 5. Le reste, c'est le contenu du Dump
        let content = String::from_utf8_lossy(&bytes[cursor..]).into_owned();

        Some(Self { library, name, esp_id, timestamp, content })
    }
}

pub struct DumpScreen {
    pub selected_library: Option<String>,
    pub selected_name: Option<String>,
    pub auto_scroll: bool,
}

impl Default for DumpScreen {
    fn default() -> Self {
        Self {
            selected_library: None,
            selected_name: None,
            auto_scroll: true,
        }
    }
}

impl DumpScreen {

    pub fn show(&mut self, ctx: &egui::Context, dumps: &[DumpEntry]) {
        // 1. EXTRACTION DES LIBRAIRIES UNIQUES
        // On utilise un BTreeSet pour les trier automatiquement par ordre alphabétique
        let mut libraries: BTreeSet<&String> = BTreeSet::new();
        for entry in dumps {
            libraries.insert(&entry.library);
        }

        // 2. PANNEAU LATÉRAL : L'explorateur dynamique
        egui::SidePanel::left("dump_explorer")
            .resizable(true)
            .default_width(200.0)
            .show(ctx, |ui| {
                ui.heading("📦 Librairies");
                ui.separator();

                ScrollArea::vertical().show(ui, |ui| {
                    // Boucle sur chaque librairie unique trouvée dans les données
                    for lib in libraries {
                        let is_lib_selected = self.selected_library.as_ref() == Some(lib);
                        
                        // Bouton principal pour la Librairie
                        if ui.selectable_label(is_lib_selected, format!("📁 {}", lib)).clicked() {
                            self.selected_library = Some(lib.clone());
                            self.selected_name = None; // Reset le sous-choix
                        }

                        // Si cette librairie est sélectionnée, on cherche ses dumps associés
                        if is_lib_selected {
                            ui.indent(lib, |ui| {
                                // Extraction des NOMS de dumps uniques pour CETTE librairie
                                let mut dump_names: BTreeSet<&String> = BTreeSet::new();
                                for entry in dumps.iter().filter(|e| &e.library == lib) {
                                    dump_names.insert(&entry.name);
                                }

                                if dump_names.is_empty() {
                                    ui.weak("Aucun dump");
                                }

                                // Boucle sur les noms de dumps de cette librairie
                                for name in dump_names {
                                    let is_name_selected = self.selected_name.as_ref() == Some(name);
                                    
                                    // Sous-bouton pour le nom du dump
                                    if ui.selectable_label(is_name_selected, format!("📄 {}", name)).clicked() {
                                        self.selected_name = Some(name.clone());
                                    }
                                }
                            });
                        }
                    }
                });
            });

        // 3. PANNEAU CENTRAL : Visualisation du contenu
        egui::CentralPanel::default().show(ctx, |ui| {
            match (&self.selected_library, &self.selected_name) {
                (Some(lib), Some(name)) => {
                    ui.horizontal(|ui| {
                        ui.heading(format!("{} ➔ {}", lib, name));
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            ui.checkbox(&mut self.auto_scroll, "Auto-scroll");
                        });
                    });
                    ui.separator();

                    // On cherche l'entrée correspondante pour l'afficher
                    // (Idéalement la plus récente si tu en reçois plusieurs avec le même nom)
                    if let Some(target_entry) = dumps.iter()
                        .filter(|e| &e.library == lib && &e.name == name)
                        .last() { // .last() prend le dernier reçu (le plus récent)
                        
                        ScrollArea::vertical()
                            .auto_shrink([false; 2])
                            .stick_to_bottom(self.auto_scroll)
                            .show(ui, |ui| {
                                // Affichage du timestamp de l'ESP32
                                let seconds = target_entry.timestamp as f64 / 1000.0;
                                ui.label(RichText::new(format!("[Timestamp ESP: {:.3} s]", seconds))
                                    .color(Color32::GRAY)
                                    .font(FontId::monospace(11.0)));
                                ui.add_space(8.0);
                                
                                egui::Frame::canvas(ui.style()).show(ui, |ui| {
                                    ui.add_sized(
                                        ui.available_size(),
                                        egui::Label::new(RichText::new(&target_entry.content).font(FontId::monospace(12.0)))
                                            .selectable(true) // Permet à l'utilisateur de copier le texte
                                            .wrap(false)      // 👈 ICI : Désactive complètement le retour à la ligne automatique
                                    );
                                });
                            });
                    }
                }
                (Some(lib), None) => {
                    ui.centered_and_justified(|ui| {
                        ui.heading(format!("👈 Sélectionne un dump de la librairie [{}]", lib));
                    });
                }
                _ => {
                    ui.centered_and_justified(|ui| {
                        ui.heading("👈 Sélectionne une librairie pour commencer");
                    });
                }
            }
        });
    }
}