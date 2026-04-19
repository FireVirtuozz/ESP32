//basic enum
enum IpAddrKind {
    V4,
    V6,
}

struct IpAddr {
    kind: IpAddrKind,
    address: String,
}

//enum associated with type (like Java)
enum IpAddrEnum {
    V4(String),
    V6(String),
}

//with tuple
enum IpAddrEnumTuple {
    V4(u8, u8, u8, u8),
    V6(String),
}

//with structs
struct Ipv4Addr {
    // --snip--
}
struct Ipv6Addr {
    // --snip--
}
enum IpAddrStruct {
    V4(Ipv4Addr),
    V6(Ipv6Addr),
}

//enum & methods
enum Message {
    Quit,
    Move { x: i32, y: i32 },
    Write(String),
    ChangeColor(i32, i32, i32),
}
impl Message {
    fn call(&self) {
        // method body would be defined here
    }
}

//option (like Java), no NULL values in Rust
/*
This type is built-in
https://doc.rust-lang.org/std/option/enum.Option.html
enum Option<T> {
    None,
    Some(T),
}
*/

//match
#[derive(Debug)] // so we can inspect the state in a minute
enum UsState {
    Alabama,
    Alaska,
    // --snip--
}
enum Coin {
    Penny,
    Nickel,
    Dime,
    Quarter(UsState),
}
fn value_in_cents(coin: Coin) -> u8 {
    match coin { //covers all the case possible or error (exhaustive)
        Coin::Penny => {
            println!("Lucky penny!");
            1 //returns 1
        } //no comma
        Coin::Nickel => 5,
        Coin::Dime => 10,
        Coin::Quarter(state) => { //value bind state
            println!("State quarter from {state:?}!");
            25
        }
    }
}

impl UsState {
    fn existed_in(&self, year: u16) -> bool {
        match self {
            UsState::Alabama => year >= 1819,
            UsState::Alaska => year >= 1959,
            // -- snip --
        }
    }
}

//other patterns than match : if let
fn describe_state_quarter(coin: Coin) -> Option<String> {
    if let Coin::Quarter(state) = coin {
        if state.existed_in(1900) {
            Some(format!("{state:?} is pretty old, for America!"))
        } else {
            Some(format!("{state:?} is relatively new."))
        }
    } else {
        None
    }
}

//let .. else
fn describe_state_quarter_else(coin: Coin) -> Option<String> {
    let Coin::Quarter(state) = coin else {
        return None;
    };

    if state.existed_in(1900) {
        Some(format!("{state:?} is pretty old, for America!"))
    } else {
        Some(format!("{state:?} is relatively new."))
    }
}

fn main() {

    //declaring enum
    let four = IpAddrKind::V4;
    let six = IpAddrKind::V6;

    let home = IpAddr {
        kind: IpAddrKind::V4,
        address: String::from("127.0.0.1"),
    };
    let loopback = IpAddr {
        kind: IpAddrKind::V6,
        address: String::from("::1"),
    };

    let home = IpAddrEnum::V4(String::from("127.0.0.1"));
    let loopback = IpAddrEnum::V6(String::from("::1"));

    let home = IpAddrEnumTuple::V4(127, 0, 0, 1);
    let loopback = IpAddrEnumTuple::V6(String::from("::1"));

    let m = Message::Write(String::from("hello"));
    m.call();

    //option
    let some_number = Some(5);
    let some_char = Some('e');
    let absent_number: Option<i32> = None; //annotation to give the type

    let x: i8 = 5;
    let y: Option<i8> = Some(5);
    //let sum = x + y; error, i8 + Option<i8>
    let sum = x + y.unwrap();
    println!("somme : {sum}");

    //option & match
    let five = Some(5);
    let six = plus_one(five);
    let none = plus_one(None);

    //match
    let dice_roll = 9;
    match dice_roll {
        3 => add_fancy_hat(),
        7 => remove_fancy_hat(),
        other => move_player(other),
    }
    match dice_roll {
        3 => add_fancy_hat(),
        7 => remove_fancy_hat(),
        _ => reroll(), // for others: _ for unused variable
    }
    match dice_roll {
        3 => add_fancy_hat(),
        7 => remove_fancy_hat(),
        _ => (), //do nothing
    }

    let config_max = Some(3u8);
    match config_max {
        Some(max) => println!("The maximum is configured to be {max}"),
        _ => (), //for None & maybe others, do nothing
    }
    //concise if let, same as binding, not checking everything --> prefer match
    if let Some(max) = config_max {
        println!("The maximum is configured to be {max}");
    }

    let coin = Coin::Quarter(UsState::Alabama);
    let mut count = 0;
    //borrowing because ownership of UsState is moved to state
    //and coin is invalid after this because state dropped!
    match &coin {
        Coin::Quarter(state) => println!("State quarter from {state:?}!"),
        _ => count += 1,
    }
    //same thing
    if let Coin::Quarter(stateLet) = &coin {
        println!("State quarter from {stateLet:?}!");
    } else {
        count += 1;
    }
}

fn plus_one(x: Option<i32>) -> Option<i32> {
    match x {
        None => None,
        Some(i) => Some(i + 1), //value bind i
    }
}

fn add_fancy_hat() {}
fn remove_fancy_hat() {}
fn move_player(num_spaces: u8) {}
fn reroll() {}