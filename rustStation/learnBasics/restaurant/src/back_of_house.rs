//module back_of_house

fn fix_incorrect_order() {
    cook_order();
    super::deliver_order(); //calling parent, even if private a child can use parent's functions
    //not like Java where private -> private to the class, even children
}

fn cook_order() {}

//public enum, everything inside is also public
pub enum Appetizer {
    Soup,
    Salad,
}

//struct can be public, things inside are private
pub struct Breakfast {
    pub toast: String,
    seasonal_fruit: String, //private field, cannot be modified from others
}

impl Breakfast {
    pub fn summer(toast: &str) -> Breakfast {
        Breakfast {
            toast: String::from(toast),
            seasonal_fruit: String::from("peaches"),
        }
    }
}
