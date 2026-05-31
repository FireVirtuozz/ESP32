use std::collections::HashMap;

pub mod udp_logs;
pub mod udp_dump;
pub mod udp_sensors;
pub mod udp_video;

const BUFFER_MAX_UDP_SIZE: usize = 1400;

// network/utils.rs

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
    puzzles: HashMap<u32, Puzzle>,
    last_completed_id: u32,
}

impl FragmentReassembler {
    pub fn new(mode: ReassemblyMode) -> Self {
        Self {
            mode,
            puzzles: HashMap::new(),
            last_completed_id: 0,
        }
    }

    /// Injecte un fragment et renvoie le vecteur complet si le puzzle est terminé
    pub fn push_fragment(&mut self, id: u32, idx: usize, total: usize, payload: Vec<u8>) -> Option<Vec<u8>> {
        // Stratégie Volatile : On ignore les paquets en retard
        if self.mode == ReassemblyMode::Volatile && id < self.last_completed_id {
            return None;
        }

        // Nettoyage automatique si la Map devient trop grosse (anti-fuite mémoire)
        if self.puzzles.len() > 5 && self.mode == ReassemblyMode::Volatile { 
            self.puzzles.retain(|&p_id, _| p_id >= id);
        }

        // Récupère ou crée le puzzle
        let puzzle = self.puzzles.entry(id).or_insert_with(|| Puzzle::new(total));
        puzzle.insert(idx, payload);

        // Si le puzzle est complet, on reconstruit et on nettoie
        if puzzle.is_complete() {
            if let Some(completed) = self.puzzles.remove(&id) {
                let full_data = completed.rebuild();
                
                if self.mode == ReassemblyMode::Volatile {
                    self.last_completed_id = id;
                }
                
                return Some(full_data);
            }
        }

        None
    }
}