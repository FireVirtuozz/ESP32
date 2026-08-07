use std::{collections::{HashMap, VecDeque}, hint::black_box};

use criterion::{BatchSize, Criterion, criterion_group, criterion_main};
use rand::{RngExt, SeedableRng, rngs::{StdRng, ThreadRng}};
use rand::Rng;
use slotmap::{DefaultKey, SecondaryMap, SlotMap};

#[derive(Clone)]
struct BigElement {
    data: [u8; 500],
}

impl BigElement {
    fn new(seed: u8) -> Self {
        Self { data: [seed; 500] }
    }
}

fn bench_buffer(c: &mut Criterion) {
    let mut group = c.benchmark_group("Remove 1 element on 1M int");

    //as vector.erase(begin) in CPP
 group.bench_function("Vec::remove(0)", |b| {
        // equivalent std::vector::erase(v.begin())
        b.iter_batched(
            || (0..1_000_000).collect::<Vec<u32>>(),
            |mut v| { black_box(v.remove(0)); },
            BatchSize::SmallInput,
        );
    });

    group.bench_function("Vec::swap_remove(0)", |b| {
        // equivalent std::swap(v[0], v.back()); v.pop_back();
        b.iter_batched(
            || (0..1_000_000).collect::<Vec<u32>>(),
            |mut v| { black_box(v.swap_remove(0)); },
            BatchSize::SmallInput,
        );
    });

    group.bench_function("Vec::retain", |b| {
        // equivalent v.erase(std::remove_if(...), v.end())
        b.iter_batched(
            || (0..1_000_000).collect::<Vec<u32>>(),
            |mut v| { v.retain(|&x| x != 0); black_box(&v); },
            BatchSize::SmallInput,
        );
    });

    group.bench_function("VecDeque::pop_front", |b| {
        // equivalent std::deque::pop_front()
        b.iter_batched(
            || (0..1_000_000).collect::<VecDeque<u32>>(),
            |mut v| { black_box(v.pop_front()); },
            BatchSize::SmallInput,
        );
    });

    group.finish();

    let mut group = c.benchmark_group("Remove 300/3000 elements");

    // indices à supprimer : un échantillon fixe, espacé régulièrement
    let indices_to_remove: Vec<usize> = (0..3000).step_by(10).collect(); // 300 indices

    group.bench_function("retain", |b| {
        b.iter_batched(
            || {
                let v: Vec<u32> = (0..3000).collect();
                let to_remove = indices_to_remove.clone();
                (v, to_remove)
            },
            |(mut v, to_remove)| {
                let set: std::collections::HashSet<usize> = to_remove.into_iter().collect();
                let mut i = 0;
                v.retain(|_| {
                    let keep = !set.contains(&i);
                    i += 1;
                    keep
                });
                black_box(&v);
            },
            BatchSize::SmallInput,
        );
    });

    group.bench_function("swap_remove (sorted desc)", |b| {
        b.iter_batched(
            || {
                let v: Vec<u32> = (0..3000).collect();
                let mut to_remove = indices_to_remove.clone();
                to_remove.sort_unstable_by(|a, b| b.cmp(a)); // décroissant
                (v, to_remove)
            },
            |(mut v, to_remove)| {
                for idx in to_remove {
                    black_box(v.swap_remove(idx));
                }
            },
            BatchSize::SmallInput,
        );
    });

    group.finish();

    let mut group = c.benchmark_group("Remove 300/3000 big elements (500B)");

    let indices_to_remove: Vec<usize> = (0..3000).step_by(10).collect();

    group.bench_function("retain", |b| {
        b.iter_batched(
            || {
                let v: Vec<BigElement> = (0..3000).map(|i| BigElement::new(i as u8)).collect();
                (v, indices_to_remove.clone())
            },
            |(mut v, to_remove)| {
                let set: std::collections::HashSet<usize> = to_remove.into_iter().collect();
                let mut i = 0;
                v.retain(|_| {
                    let keep = !set.contains(&i);
                    i += 1;
                    keep
                });
                black_box(&v);
            },
            BatchSize::SmallInput,
        );
    });

    group.bench_function("swap_remove (sorted desc)", |b| {
        b.iter_batched(
            || {
                let v: Vec<BigElement> = (0..3000).map(|i| BigElement::new(i as u8)).collect();
                let mut to_remove = indices_to_remove.clone();
                to_remove.sort_unstable_by(|a, b| b.cmp(a));
                (v, to_remove)
            },
            |(mut v, to_remove)| {
                for idx in to_remove {
                    black_box(v.swap_remove(idx));
                }
            },
            BatchSize::SmallInput,
        );
    });

    group.finish();

let mut group = c.benchmark_group("Filtering & Removal Comparison (3000 elements)");

    // Setup Données Vec + HashMap
    let setup_hashmap = || {
        let elements: Vec<BigElement> = (0..3000).map(|i| BigElement::new(i as u8)).collect();
        let mut rng = StdRng::seed_from_u64(42);
        let map: HashMap<usize, f64> = (0..3000)
            .map(|i| (i, rng.random_range(0.0..2.0)))
            .collect();
        (elements, map)
    };

    // Setup Données SlotMap + SecondaryMap
    let setup_slotmap = || {
        let mut sm: SlotMap<DefaultKey, BigElement> = SlotMap::with_capacity(3000);
        let mut sec_map: SecondaryMap<DefaultKey, f64> = SecondaryMap::new();
        let mut rng = StdRng::seed_from_u64(42);

        for i in 0..3000 {
            let key = sm.insert(BigElement::new(i as u8));
            sec_map.insert(key, rng.random_range(0.0..2.0));
        }
        (sm, sec_map)
    };

    // 1. Vec + HashMap + swap_remove
    group.bench_function("1. Vec + HashMap + swap_remove", |b| {
        b.iter_batched(
            setup_hashmap,
            |(mut elements, map)| {
                let mut idx_to_remove = Vec::new();
                for (idx, time) in &map {
                    if *time > 1.5 {
                        idx_to_remove.push(*idx);
                    }
                }

                idx_to_remove.sort_unstable_by(|a, b| b.cmp(a));
                for idx in idx_to_remove {
                    black_box(elements.swap_remove(idx));
                }
                black_box(&elements);
            },
            BatchSize::SmallInput,
        );
    });

    // 2. Vec + HashMap + retain
    group.bench_function("2. Vec + HashMap + retain", |b| {
        b.iter_batched(
            setup_hashmap,
            |(mut elements, map)| {
                let mut current_idx = 0;
                elements.retain(|_| {
                    let time = map.get(&current_idx).copied().unwrap_or(0.0);
                    current_idx += 1;
                    time <= 1.5
                });
                black_box(&elements);
            },
            BatchSize::SmallInput,
        );
    });

    // 3. SlotMap + SecondaryMap + retain
    group.bench_function("3. SlotMap + retain", |b| {
        b.iter_batched(
            setup_slotmap,
            |(mut sm, sec_map)| {
                sm.retain(|key, _| {
                    let time = sec_map.get(key).copied().unwrap_or(0.0);
                    time <= 1.5
                });
                black_box(&sm);
            },
            BatchSize::SmallInput,
        );
    });

    // 4. SlotMap + Collect keys + remove O(1)
    group.bench_function("4. SlotMap + keys collect + remove", |b| {
        b.iter_batched(
            setup_slotmap,
            |(mut sm, sec_map)| {
                let keys_to_remove: Vec<DefaultKey> = sm
                    .keys()
                    .filter(|&key| sec_map.get(key).copied().unwrap_or(0.0) > 1.5)
                    .collect();

                for key in keys_to_remove {
                    black_box(sm.remove(key));
                }
                black_box(&sm);
            },
            BatchSize::SmallInput,
        );
    });

    group.finish();


}

criterion_group!(benches, bench_buffer);
criterion_main!(benches);