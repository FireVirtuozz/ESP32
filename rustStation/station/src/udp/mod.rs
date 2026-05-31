use std::collections::HashMap;

use crate::error::AppError;

pub mod udp_logs;
pub mod udp_dump;
pub mod udp_sensors;
pub mod udp_video;

const BUFFER_MAX_UDP_SIZE: usize = 1400;
pub const HEADER_FRAGMENT_SIZE: usize = 4 + 1 + 1 + 1;

pub struct HeaderUdpFragment {
    pub frag_id: u32,
    pub frag_total: u8,
    pub frag_idx: u8,
    pub esp_id: u8,
}

impl HeaderUdpFragment {
    pub fn header_fragment_parse(buf: &[u8]) -> Result<Self, AppError> {
        if buf.len() < HEADER_FRAGMENT_SIZE {
            return Err("Header not valid".into())
        }

        Ok(Self {
            frag_id:    u32::from_be_bytes([buf[0], buf[1], buf[2], buf[3]]),
            frag_total:  buf[4],
            frag_idx:    buf[5],
            esp_id:      buf[6],
        })
    }
}

pub struct Puzzle {
    slots: Vec<Option<Vec<u8>>>,
    filled_count: usize,
}

impl Puzzle {
    pub fn new(total_fragments: usize) -> Self {
        Self {
            slots: vec![None; total_fragments],
            filled_count: 0,
        }
    }

    /// Tente d'insérer un fragment. Renvoie true s'il venait d'être ajouté.
    pub fn insert(&mut self, idx: usize, payload: Vec<u8>) -> bool {
        if idx >= self.slots.len() { return false; }
        
        if self.slots[idx].is_none() {
            self.slots[idx] = Some(payload);
            self.filled_count += 1;
            true
        } else {
            false // Fragment déjà reçu (doublon UDP)
        }
    }

    pub fn is_complete(&self) -> bool {
        self.filled_count == self.slots.len()
    }

    /// Consomme le puzzle et fusionne tous les morceaux en un seul vecteur
    pub fn rebuild(self) -> Vec<u8> {
        let mut full_data = Vec::new();
        for chunk in self.slots {
            if let Some(data) = chunk {
                full_data.extend(data);
            }
        }
        full_data
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum ReassemblyMode {
    Volatile,
    Strict,
}

pub struct FragmentReassembler {
    mode: ReassemblyMode,
    puzzles: HashMap<(u8, u32), Puzzle>, 
    last_completed_ids: HashMap<u8, u32>,
}

impl FragmentReassembler {
    pub fn new(mode: ReassemblyMode) -> Self {
        Self {
            mode,
            puzzles: HashMap::new(),
            last_completed_ids: HashMap::new(),
        }
    }

    /// Injecte un fragment et renvoie le vecteur complet si le puzzle est terminé
    // Dans ton fichier de gestion UDP
    pub fn push_fragment(&mut self, header: HeaderUdpFragment, payload: Vec<u8>) -> Option<Vec<u8>> {
        // Stratégie Volatile : On ignore les paquets en retard
        if self.mode == ReassemblyMode::Volatile {
            if let Some(&last_id) = self.last_completed_ids.get(&header.esp_id) {
                if header.frag_id < last_id {
                    return None;
                }
            }
        }

        // NETTOYAGE SÉCURISÉ ANTI-FUITE MÉMOIRE
        if self.puzzles.len() > 10 {
            if self.mode == ReassemblyMode::Volatile {
                self.puzzles.retain(|&(e_id, p_id), _| e_id != header.esp_id || p_id >= header.frag_id);
            } else {
                // Mode Strict : Si on a trop de puzzles en cours, on vire ceux qui ont un retard 
                // de plus de 5 IDs par rapport au frag_id qu'on vient de recevoir pour cet ESP.
                self.puzzles.retain(|&(e_id, p_id), _| {
                    if e_id == header.esp_id {
                        // Évite l'overflow si frag_id est proche de 0 lors d'un reboot de l'ESP
                        header.frag_id.saturating_sub(p_id) <= 5 
                    } else {
                        true // On ne touche pas aux puzzles des autres ESP
                    }
                });
            }
        }

        let puzzle = self.puzzles.entry((header.esp_id, header.frag_id))
            .or_insert_with(|| Puzzle::new(header.frag_total as usize));
        puzzle.insert(header.frag_idx as usize, payload);

        if puzzle.is_complete() {
            if let Some(completed) = self.puzzles.remove(&(header.esp_id, header.frag_id)) {
                let full_data = completed.rebuild();
                
                if self.mode == ReassemblyMode::Volatile {
                    self.last_completed_ids.insert(header.esp_id, header.frag_id);
                }
                
                return Some(full_data);
            }
        }

        None
    }
}