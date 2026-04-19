use std::sync::mpsc;
use std::thread;
use std::time::Duration;

fn main() {
    //channel to communicate between threads
    //mpsc: multiple producers, single consumer
    let (tx, rx) = mpsc::channel();

    let tx1 = tx.clone();
    //move ownership of tx in closure
    thread::spawn(move || {
        let val = String::from("hi");
        tx1.send(val).unwrap();

        //ownership of val is in send, we can't use it after
        //println!("val is {val}");

        thread::sleep(Duration::from_secs(1));

        let vals = vec![
            String::from("hi"),
            String::from("from"),
            String::from("the"),
            String::from("thread"),
        ];

        for val in vals {
            tx1.send(val).unwrap();
            thread::sleep(Duration::from_secs(1));
        }
    });

    thread::spawn(move || {
        let vals = vec![
            String::from("more"),
            String::from("messages"),
            String::from("for"),
            String::from("you"),
        ];

        for val in vals {
            tx.send(val).unwrap();
            thread::sleep(Duration::from_secs(1));
        }
    });

    //drop(tx);

    let received = rx.recv().unwrap();
    println!("Got: {received}");

    for received in rx {
        println!("Got: {received}");
    }
}