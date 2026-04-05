struct User {
    active: bool,
    username: String, //store references &str, for after
    email: String,
    sign_in_count: u64,
}

struct Color(i32, i32, i32);
struct Point(i32, i32, i32);


struct AlwaysEqual;

#[derive(Debug)] //to print debug with ":?"
struct Rectangle {
    width: u32,
    height: u32,
}

//methods & associated functions
impl Rectangle { //Rectangle type implements
    fn area(&self) -> u32 { //this is a method (borrows self as argument)
        self.width * self.height
    }

    fn width(&self) -> bool {
        self.width > 0
    }

    fn can_hold(&self, other: &Rectangle) -> bool {
        self.width > other.width && self.height > other.height
    }

    //this is an associated function
    fn square(size: u32) -> Self { //it is like a "new" in Java, creating an instance
        Self {
            width: size,
            height: size,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn larger_can_hold_smaller() {
        let larger = Rectangle {
            width: 8,
            height: 7,
        };
        let smaller = Rectangle {
            width: 5,
            height: 1,
        };

        assert!(larger.can_hold(&smaller));
    }

    #[test]
    fn smaller_cannot_hold_larger() {
        let larger = Rectangle {
            width: 8,
            height: 7,
        };
        let smaller = Rectangle {
            width: 5,
            height: 1,
        };

        assert!(!smaller.can_hold(&larger));
    }
}

fn main() {
    //declare instance struct
    let mut user1 = User {
        active: true,
        username: String::from("someusername123"),
        email: String::from("someone@example.com"),
        sign_in_count: 1,
    };
    user1.email = String::from("anotheremail@example.com");

    //declare struct from another
    let user2 = User {
        email: String::from("another@example.com"),
        ..user1 //with other values of user1
    };

    //tuple structs (named tuple)
    let black = Color(0, 0, 0);
    let origin = Point(0, 0, 0);

    //unit-like structs
    let subject = AlwaysEqual;

    //examples
    //tuples
    let rect1 = (30, 50);
    println!(
        "The area of the rectangle is {} square pixels.",
        area_tuple(rect1)
    );

    //struct
    let rect1 = Rectangle {
        width: 30,
        height: 50,
    };
    println!(
        "The area of the rectangle is {} square pixels.",
        area_struct(&rect1)
    );

    //print debug (stdout)
    println!("rect1 is {rect1:?}"); //takes reference
    println!("rect1 is {rect1:#?}");

    //print debug (stderr)
    let scale = 2;
    let rect1 = Rectangle {
        width: dbg!(30 * scale), //takes & return ownership, outputs this value
        height: 50,
    };
    dbg!(&rect1); //outputs whole value

    //methods
    let rect1 = Rectangle {
        width: 30,
        height: 50,
    };
    println!(
        "The area of the rectangle is {} square pixels.",
        rect1.area() //call method
    );
    if rect1.width() {
        println!("The rectangle has a nonzero width; it is {}", rect1.width);
    }

    let rect1 = Rectangle {
        width: 30,
        height: 50,
    };
    let rect2 = Rectangle {
        width: 10,
        height: 40,
    };
    let rect3 = Rectangle {
        width: 60,
        height: 45,
    };
    println!("Can rect1 hold rect2? {}", rect1.can_hold(&rect2));
    println!("Can rect1 hold rect3? {}", rect1.can_hold(&rect3));

    //associated function
    let sq = Rectangle::square(3); //like "new", :: is for namespaces & associated funcs
    println!("square : {sq:#?}");
}

fn build_user(email: String, username: String) -> User {
    User {
        active: true,
        username,
        email, //same name for field & variable, write once
        sign_in_count: 1,
    }
}

fn area_tuple(dimensions: (u32, u32)) -> u32 {
    dimensions.0 * dimensions.1
}

//borrowing, so that main retains ownership
fn area_struct(rectangle: &Rectangle) -> u32 {
    rectangle.width * rectangle.height
}
