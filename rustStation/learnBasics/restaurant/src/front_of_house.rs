
//module front_of_house

//serving child of front_of_house
//public module (inside is not public)
mod serving { //private to parents by default
    fn take_order() {}

    fn serve_order() {}

    fn take_payment() {}
}

//declare hosting as public child of front_of_house -> front_of_house/hosting.rs
pub mod hosting;