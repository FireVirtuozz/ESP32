use std::collections::BTreeSet;

use egui::{Color32, FontId, RichText, ScrollArea};

use crate::gui::ScreensTypes;

#[derive(Debug)]
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
        let raw_content = &bytes[cursor..];

        // On parcourt les octets et on remplace les 0 par des sauts de ligne '\n' (0x0A en ASCII)
        let cleaned_bytes: Vec<u8> = raw_content
            .iter()
            .map(|&b| if b == 0 { b'\n' } else { b })
            .collect();

        let content = String::from_utf8_lossy(&cleaned_bytes).into_owned();

        Some(Self { library, name, esp_id, timestamp, content })
    }
}

pub struct DumpScreen {
    pub selected_esp_id: Option<u8>,
    pub selected_library: Option<String>,
    pub selected_name: Option<String>,
    pub auto_scroll: bool,
}

impl Default for DumpScreen {
    fn default() -> Self {
        Self {
            selected_esp_id: None,
            selected_library: None,
            selected_name: None,
            auto_scroll: true,
        }
    }
}
impl DumpScreen {
    pub fn show(&mut self, ctx: &egui::Context, dumps: &[DumpEntry]) {
        
        // 1. PANNEAU LATÉRAL : Fixe et stable (exact_width + resizable_false)
        egui::SidePanel::left("dump_explorer")
            .resizable(false)   // Empêche le panneau de bouger tout seul
            .exact_width(240.0)  // Largeur stricte et stable
            .show(ctx, |ui| {
                
                // --- SECTION 1 : SÉLECTION DE L'ESP ID ---
                ui.heading("📱 Appareils");
                ui.add_space(4.0);

                // Extraction de tous les ESP ID uniques présents dans les dumps
                let mut esp_ids: BTreeSet<u8> = dumps.iter().map(|e| e.esp_id).collect();

                egui::ComboBox::from_id_source("esp_id_selector")
                    .selected_text(match self.selected_esp_id {
                        Some(id) => format!("ESP32 (ID: {})", id),
                        None => "Choisir un appareil...".to_string(),
                    })
                    .show_ui(ui, |ui| {
                        for id in esp_ids {
                            if ui.selectable_value(&mut self.selected_esp_id, Some(id), format!("ESP ID: {}", id)).clicked() {
                                // Si on change d'ESP, on reset les sous-choix pour éviter les bugs
                                self.selected_library = None;
                                self.selected_name = None;
                            }
                        }
                    });
                
                ui.add_space(10.0);
                ui.heading("📦 Librairies");
                ui.separator();

                // --- SECTION 2 : AFFICHAGE DES LIBRARIERIES FILTRÉES ---
                if let Some(target_esp_id) = self.selected_esp_id {
                    // On ne prend que les librairies de l'ESP sélectionné
                    let mut libraries: BTreeSet<&String> = BTreeSet::new();
                    for entry in dumps.iter().filter(|e| e.esp_id == target_esp_id) {
                        libraries.insert(&entry.library);
                    }

                    ScrollArea::vertical().show(ui, |ui| {
                        for lib in libraries {
                            let is_lib_selected = self.selected_library.as_ref() == Some(lib);
                            
                            if ui.selectable_label(is_lib_selected, format!("📁 {}", lib)).clicked() {
                                self.selected_library = Some(lib.clone());
                                self.selected_name = None;
                            }

                            if is_lib_selected {
                                ui.indent(lib, |ui| {
                                    let mut dump_names: BTreeSet<&String> = BTreeSet::new();
                                    for entry in dumps.iter().filter(|e| e.esp_id == target_esp_id && &e.library == lib) {
                                        dump_names.insert(&entry.name);
                                    }

                                    for name in dump_names {
                                        let is_name_selected = self.selected_name.as_ref() == Some(name);
                                        if ui.selectable_label(is_name_selected, format!("📄 {}", name)).clicked() {
                                            self.selected_name = Some(name.clone());
                                        }
                                    }
                                });
                            }
                        }
                    });
                } else {
                    ui.weak("Veuillez sélectionner un ESP ci-dessus.");
                }
            });

        // 2. PANNEAU CENTRAL : Le Terminal de Log
        egui::CentralPanel::default().show(ctx, |ui| {
            match (self.selected_esp_id, &self.selected_library, &self.selected_name) {
                (Some(id), Some(lib), Some(name)) => {
                    // Header du log
                    ui.horizontal(|ui| {
                        ui.heading(format!("ESP {} ➔ {} ➔ {}", id, lib, name));
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            ui.checkbox(&mut self.auto_scroll, "Auto-scroll");
                        });
                    });
                    ui.separator();

                    // Récupération des entrées correspondantes
                    let matching_entries: Vec<&DumpEntry> = dumps.iter()
                        .filter(|e| e.esp_id == id && &e.library == lib && &e.name == name)
                        .collect();

                    if !matching_entries.is_empty() {
                        let mut full_text = String::new();
                        for entry in &matching_entries {
                            full_text.push_str(&entry.content);
                        }

                        // Style "Console de Dev"
                        egui::Frame::none()
                            .fill(Color32::from_rgb(15, 15, 15)) // Fond sombre type terminal
                            .inner_margin(8.0)
                            .show(ui, |ui| {
                                ScrollArea::vertical()
                                    .auto_shrink([false; 2])
                                    .stick_to_bottom(self.auto_scroll)
                                    .show(ui, |ui| {
                                        // On force l'alignement Top-Down et à Gauche (Align::Min)
                                        ui.with_layout(egui::Layout::top_down(egui::Align::Min), |ui| {
                                            
                                            // Utilisation d'un TextEdit multiline en lecture seule : 
                                            // C'est le meilleur composant egui pour afficher des pavés de logs textuels purs.
                                            let mut text_ref = full_text.as_str();
                                            ui.add(
                                                egui::TextEdit::multiline(&mut text_ref)
                                                    .font(FontId::monospace(13.0)) // Police console
                                                    .text_color(Color32::from_rgb(220, 220, 220)) // Texte blanc cassé
                                                    .frame(false) // Retire la bordure de saisie de texte
                                                    .desired_width(f32::INFINITY) // Prend toute la largeur dispo
                                            );
                                        });
                                    });
                            });
                    } else {
                        ui.label("Aucune donnée reçue pour ce flux.");
                    }
                }
                (Some(_), Some(lib), None) => {
                    ui.centered_and_justified(|ui| {
                        ui.heading(format!("👈 Sélectionne un dump dans la librairie [{}]", lib));
                    });
                }
                (Some(_), None, _) => {
                    ui.centered_and_justified(|ui| {
                        ui.heading("👈 Sélectionne une librairie");
                    });
                }
                _ => {
                    ui.centered_and_justified(|ui| {
                        ui.heading("👈 Sélectionne un appareil ESP pour commencer");
                    });
                }
            }
        });
    }
}