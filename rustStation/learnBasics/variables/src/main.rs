use std::io;

//return i32, no semicolon for the return value
fn five() -> i32 {
    5
}

fn main() {

    //immutable variable by default
    let x = 5;
    println!("The value of x is: {x}");
    /*x = 6; variable x is immutable, it won't work*/
    println!("The value of x is: {x}");

    //mutable variable
    let mut y = 5;
    println!("The value of y is: {y}");
    y = 6;
    println!("The value of y is: {y}");

    //constants : annotation is mandatory
    const THREE_HOURS_IN_SECONDS: u32 = 60 * 60 * 3;

    //shadowing : creates new variables with same name
    //immutable but changes can be made & stay immutable after
    let z = 5;
    let z = z + 1;

    {
        let z = z * 2;
        println!("The value of z in the inner scope is: {z}");
    }

    println!("The value of z is: {z}");

    //shadowing works with different types
    let spaces = "   ";
    let spaces = spaces.len();

    //not mut
    let mut spaces = "   ";
    /*spaces = spaces.len(); error, not same type*/

    //annotation is mandatory because of the parse, it needs to know the type before using
    let guess: u32 = "42".parse().expect("Not a number!");

    //types
    let int8 : i8;
    let uint8 : u8;
    let int16 : i16;
    let uint16 : u16;
    let int32 : i32;
    let uint32 : u32;
    let int64 : i64;
    let uint64 : u64;
    let int128 : i128;
    let uint128 : u128;
    let int : isize;
    let uint : usize;

    let decimal = 98_222; //i32 by default
    let hexa = 0xff;
    let octal = 0o77;
    let binary = 0b1111_0000;
    let byte = b'A'; //u8

    let f = 2.0; //f64 by default
    let h: f32 = 3.0; //f32

    let b: bool = true; //size : 1 byte --> still use bitmask for multiple bools

    let c: char = 'y'; //4 bytes

    //tuples
    let tup: (i32, f64, u8) = (500, 6.4, 1);
    let (x, y, z) = tup; //destructuring
    println!("The value of y is: {y}");
    let five_hundred = tup.0; //access to elements
    let six_point_four = tup.1;
    let one = tup.2;

    //arrays
    let a = [1, 2, 3, 4, 5];
    let a: [i32; 5] = [1, 2, 3, 4, 5];
    let a = [3; 5]; //[3,3,3,3,3]
    let first = a[0];

    println!("Please enter an array index.");

    let mut index = String::new();

    io::stdin()
        .read_line(&mut index)
        .expect("Failed to read line");

    let index: usize = index
        .trim()
        .parse()
        .expect("Index entered was not a number");

    let element = a[index];

    println!("The value of the element at index {index} is: {element}");
    //if entering out of bounds index, runtime exception "panic"

    another_function(3, 's');

    //expression added that returns value
    let y = {
        let x = 3;
        x + 1 //no ";" because it "returns" this value, this is an expression, not a statement
    };
    println!("The value of y is: {y}");

    let x = five();
    println!("The value of x is: {x}");
    let x = plus_one(5);
    println!("The value of x is: {x}");


    let number = 3;
    if number < 5 {
        println!("condition was true");
    } else if number == 2 {
        println!("number is 2");
    } else {
        println!("condition was false");
    }

    //if number { //we can't do this, rust does not cast to bool (nor treat >0 as "true")
    if number != 0 {
        println!("other than 0");
    }

    /*
    infinite loop
    loop {
        println!("again!");
    }*/

    //simple return loop
    let mut counter = 0;
    let result = loop {
        counter += 1;
        if counter == 10 {
            break counter * 2; //return value when break (statement here)
        }
    };
    println!("The result is {result}");

    //nested named loop (as ADA)
    let mut count = 0;
    'counting_up: loop { //name: with '
        println!("count = {count}");
        let mut remaining = 10;
        loop {
            println!("remaining = {remaining}");
            if remaining == 9 {
                break; //exit scope loop
            }
            if count == 2 {
                break 'counting_up;
            }
            remaining -= 1;
        }
        count += 1;
    }
    println!("End count = {count}");

    //while
    let mut number = 3;
    while number != 0 {
        println!("{number}!");

        number -= 1;
    }
    println!("LIFTOFF!!!");

    //array looping
    let a = [10, 20, 30, 40, 50];
    for element in a {
        println!("the value is: {element}");
    }

    //reverse range (like ADA)
    for number in (1..4).rev() {
        println!("{number}!");
    }
    println!("LIFTOFF!!!");

    let cel = convert_cel_to_far(30.0);
    println!("30.0°C = {cel} far");
    let far = convert_far_to_cel(20.0);
    println!("20.0far = {far}°C");

    let fibo_n = fibo(10);
    println!("F_10 = {fibo_n}");
}

fn another_function(x : i32, unit : char) {
    println!("The value of x is {x}{unit}");
}

fn plus_one(x: i32) -> i32 {
    x + 1
}

fn convert_far_to_cel(float : f64) -> f64 {
    (float - 32.0) / 1.8
}

fn convert_cel_to_far(float : f64) -> f64 {
    float * 1.8 + 32.0
}

fn fibo(n : i64) -> i64 {
    //assert n >= 2
    let mut f0 = 0;
    let mut f1 = 1;
    for i in (2 .. n + 1) {
        let temp = f1;
        f1 = temp + f0;
        f0 = temp;
    }
    f1
}