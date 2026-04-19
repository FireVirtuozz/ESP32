use crate::garden::vegetables::Asparagus;


//code within a module is private by default
//pub (public) allows to use outside the code
pub mod garden;

fn main() {
    let plant = Asparagus {};
    println!("I'm growing {plant:?}!");
}