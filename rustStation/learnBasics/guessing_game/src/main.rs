use std::io; //include <stdio.h>

use std::cmp::Ordering;

use rand::Rng; //include Rng from rand

//https://doc.rust-lang.org/std/prelude/index.html

fn main() {
    println!("Guess nb!");

    // thread_rng : random nb gen (like new Random() in Java)
    let secret_number = rand::thread_rng().gen_range(1..=100);
    //default : i32
    //on the stack

    println!("The secret nb is: {secret_number}");

    loop {

        println!("Input ur guess:");

        let mut guess = String::new(); //variable that can be modified (mut)
        //String::with_capacity(10); if size known before
        //String::new -> on the heap, pointer, memory allocated with predefined size

        // Deconstruct the String into parts.
        //let (ptr, len, capacity) = guess.into_raw_parts();
        //at each "guess" changes, realloc is done! so "capacity" is updated

        io::stdin() //stdin as scanf
            .read_line(&mut guess) //returns Result (Ok/Err)
            .expect("fail read line"); //crash program & display if Err, or return value if Ok
        //expect is mandatory

        println!("Size allocated for guess string on Heap: {}", guess.capacity());

        //shadowing "guess" name : rust allows two same names for different variables
        //: annotation

        //let guess: u32 = guess.trim().parse().expect("Please type a number!");
        //let guess = guess.trim().parse::<u32>().expect("pls type a nb");

        //trim to remove \r\n & trailspaces to have a correct parse
        //::<T> gives the specified type

        let guess: u32 = match guess.trim().parse() {
            Ok(num) => num, //if ok, return num
            Err(_) => continue, //restart loop
        };

        //panic : when overflow, to handle

        println!("U guessed : {guess}");

        //pass a reference of secret_number in cmp
        match guess.cmp(&secret_number) { //compare (strcmp) & match (switch case)
            Ordering::Equal => { //case Equal from Ordering's Enum
                println!("Won");
                break;
            }
            Ordering::Greater => println!("Greater"),
            Ordering::Less => {
                println!("Less");
            }
        }
    }
}
