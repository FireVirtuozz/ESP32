use std::thread;
use std::time::Duration;

fn main() {
    let v = vec![1, 2, 3];


    //spawn a thread, with closure
    //move means the closure will take ownerhip of used variables, here v
    let handle = thread::spawn(move || {
        for i in 1..10 {
            println!("hi number {i} from the spawned thread!");
            println!("Here's a vector: {v:?}");
            thread::sleep(Duration::from_millis(1000));
        }
    });

    for i in 1..5 {
        println!("hi number {i} from the main thread!");
        thread::sleep(Duration::from_millis(1000));
    }

    // the ownership is taken in the closure, so we can't drop it
    //drop(v);

    //wait for thread to stop
    //or the main program kills threads (like deamon thread in java)
    handle.join().unwrap();
}