//signatures

//by default, all is private, pub is for public

mod front_of_house;

mod back_of_house;

fn deliver_order() {}

//re-export with pub (external code can just call restaurant::hosting::add_to_waitlist())
pub use crate::front_of_house::hosting;

mod customer {
    pub fn eat_at_restaurant() {
        //hosting::add_to_waitlist(); use of hosting not valid here
    }
}

//lib is sibling of front_of_house
pub fn eat_at_restaurant() {
    // Absolute path : / = crate
    crate::front_of_house::hosting::add_to_waitlist();

    // Relative path
    front_of_house::hosting::add_to_waitlist();

    //hosting imported with use 
    hosting::add_to_waitlist();

    // Order a breakfast in the summer with Rye toast.
    let mut meal = back_of_house::Breakfast::summer("Rye");
    // Change our mind about what bread we'd like.
    meal.toast = String::from("Wheat");
    println!("I'd like {} toast please", meal.toast);

    // The next line won't compile if we uncomment it; we're not allowed
    // to see or modify the seasonal fruit that comes with the meal.
    //meal.seasonal_fruit = String::from("blueberries");

    let order1 = back_of_house::Appetizer::Soup;
    let order2 = back_of_house::Appetizer::Salad;
}

//exception for same item names (here, Result for instance)
use std::fmt;
use std::io::Result as IoResult; //rename as an alternative

fn function1() -> fmt::Result {
    Ok(())
}

fn function2() -> IoResult<()> {
    Ok(())
}

//nested path
use std::{cmp::Ordering, io};
//use std::io::{self, Write};

//all public items
use std::collections::*;


