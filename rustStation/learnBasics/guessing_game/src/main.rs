use std::io; //include <stdio.h>

use std::cmp::Ordering;

use rand::Rng; //include Rng from rand

//new type between 1 & 100
pub struct Guess {
    value: u32,
}

impl Guess {
    pub fn new(value: u32) -> Guess {
        if value < 1 {
            panic!(
                "Guess value must be greater than or equal to 1, got {value}."
            );
        } else if value > 100 {
            panic!(
                "Guess value must be less than or equal to 100, got {value}."
            );
        }
        Guess { value }
    }
    pub fn value(&self) -> u32 {
        self.value
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    //test when function should panic, with precise expectation
    #[test]
    #[should_panic(expected = "less than or equal to 100")] //substring of original
    fn greater_than_100() {
        Guess::new(200);
    }
}

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

        let guess : Guess = Guess::new(guess);

        //panic : when overflow, to handle

        println!("U guessed : {}", guess.value());

        //pass a reference of secret_number in cmp
        match guess.value.cmp(&secret_number) { //compare (strcmp) & match (switch case)
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
