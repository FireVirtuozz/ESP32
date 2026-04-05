use std::sync::{Arc, Mutex};
use std::thread;

fn main() {
    let m = Mutex::new(5);

    {
        //acquire mutex, panic if failed to take lock
        let mut num = m.lock().unwrap();
        *num = 6;
    }

    println!("m = {m:?}");

    let counter = Arc::new(Mutex::new(0));
    let mut handles = vec![];

    for _ in 0..10 {
        //we can't move ownership of counter into multiple threads
        //so we have to use Arc(Atomic Reference Counting) to allow multiple ownerships
        let counter = Arc::clone(&counter);
        let handle = thread::spawn(move || {
            let mut num = counter.lock().unwrap();

            *num += 1;
        });
        handles.push(handle);
    }

    for handle in handles {
        handle.join().unwrap();
    }

    println!("Result: {}", *counter.lock().unwrap());
}