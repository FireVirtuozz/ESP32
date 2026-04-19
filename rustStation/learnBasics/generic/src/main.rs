use generic::{SocialPost, Summary, SummaryDefault, NewsArticle};
use std::fmt::Display;

//generic struct
struct Point<T> {
    x: T,
    y: T,
}

impl<T> Point<T> {
    fn x(&self) -> &T {
        &self.x
    }
}

//method for f32 types
impl Point<f32> {
    fn distance_from_origin(&self) -> f32 {
        (self.x.powi(2) + self.y.powi(2)).sqrt()
    }
}

//mutliple generic struct
struct PointMult<T, U> {
    x: T,
    y: U,
}

struct PointMix<X1, Y1> {
    x: X1,
    y: Y1,
}

impl<X1, Y1> PointMix<X1, Y1> {
    fn mixup<X2, Y2>(self, other: PointMix<X2, Y2>) -> PointMix<X1, Y2> {
        PointMix {
            x: self.x,
            y: other.y,
        }
    }
}

//function for i32
fn largest_func(list: &[i32]) -> &i32 {
    let mut largest = &list[0];

    for item in list {
        if item > largest {
            largest = item;
        }
    }

    largest
}

//lifetime structs, holding references
struct ImportantExcerpt<'a> {
    part: &'a str,
}

//lifetime for methods
impl<'a> ImportantExcerpt<'a> {
    fn level(&self) -> i32 {
        3
    }
    //difference here from fn!!
    //no lifetimes needed cuz it take the "self"'s lifetime
        fn announce_and_return_part(&self, announcement: &str) -> &str {
        println!("Attention please: {announcement}");
        self.part
    }
}

fn main() {

    //duplication of code = dangerous
    let number_list = vec![34, 50, 25, 100, 65];

    let mut largest = &number_list[0];

    for number in &number_list {
        if number > largest {
            largest = number;
        }
    }

    println!("The largest number is {}", largest);

    let number_list = vec![102, 34, 6000, 89, 54, 2, 43, 8];

    let mut largest = &number_list[0];

    for number in &number_list {
        if number > largest {
            largest = number;
        }
    }

    println!("The largest number is {}", largest);

    //so function to avoid duplicating code
    let number_list = vec![34, 50, 25, 100, 65];

    let result = largest_func(&number_list);
    println!("The largest number is {}", result);

    let number_list = vec![102, 34, 6000, 89, 54, 2, 43, 8];

    let result = largest_func(&number_list);
    println!("The largest number is {}", result);

    //but for other types, overloading..
    let char_list = vec!['y', 'm', 'a', 'q'];

    let result = largest_char(&char_list);
    println!("The largest char is {}", result);

    //generic structs
    let integer = Point { x: 5, y: 10 };
    let float = Point { x: 1.0, y: 4.0 };

    //not different types (T = one type)
    //let wont_work = Point { x: 5, y: 4.0 };

    //multiple generic
    let both_integer = PointMult { x: 5, y: 10 };
    let both_float = PointMult { x: 1.0, y: 4.0 };
    let integer_and_float = PointMult{ x: 5, y: 4.0 };


    //method & generic
    let p = Point { x: 5, y: 10 };
    println!("p.x = {}", p.x());

    //multiple generic mehtod
    let p1 = PointMix { x: 5, y: 10.4 };
    let p2 = PointMix { x: "Hello", y: 'c' };
    let p3 = p1.mixup(p2);
    println!("p3.x = {}, p3.y = {}", p3.x, p3.y);

    //performance with generic types stay the same
    //for Option<T> for example, each type is reduced to Option_i32 etc
    //this is monomorphization


    //trait ~ interface use
    let post = SocialPost {
        username: String::from("horse_ebooks"),
        content: String::from(
            "of course, as you probably already know, people",
        ),
        reply: false,
        repost: false,
    };
    println!("1 new post: {}", post.summarize());

    //trait with default
    let article = NewsArticle {
        headline: String::from("Penguins win the Stanley Cup Championship!"),
        location: String::from("Pittsburgh, PA, USA"),
        author: String::from("Iceburgh"),
        content: String::from(
            "The Pittsburgh Penguins once again are the best \
             hockey team in the NHL.",
        ),
    };
    println!("New article available! {}", article.summarize_default());

    //lifetime usage
    let string1 = String::from("long string is long");
    {
        let string2 = String::from("xyz");
        let result = longest(string1.as_str(), string2.as_str());
        println!("The longest string is {result}");
    }
    /*
    Error cuz lifetime of string2 < result
    But return value is minimal lifetime of the parameters (for safety!)

    let string1 = String::from("long string is long");
    let result;
    {
        let string2 = String::from("xyz");
        result = longest(string1.as_str(), string2.as_str());
    }
    println!("The longest string is {result}");
     */

    let novel = String::from("Call me Ishmael. Some years ago...");
    let first_sentence = novel.split('.').next().unwrap();
    let i = ImportantExcerpt {
        part: first_sentence,
    };

    //static lifetime
    let s: &'static str = "I have a static lifetime.";

}

//function for char slice
fn largest_char(list: &[char]) -> &char {
    let mut largest = &list[0];

    for item in list {
        if item > largest {
            largest = item;
        }
    }

    largest
}

//generic function
//types have to implement PartialOrd trait to compare them (>), not all types implement this
fn largest_gen<T>(list: &[T]) -> &T {
    let mut largest = &list[0];

    /*
    for item in list {
        if item > largest {
            largest = item;
        }
    }
    */

    largest
}


//lifetime function (lifetime just in signature)
//lifetime needed cuz the return type does not know whether 
//it is from x or y
fn longest<'a>(x: &'a str, y: &'a str) -> &'a str {
    if x.len() > y.len() { x } else { y }
}

//mix of ch10
fn longest_with_an_announcement<'a, T>(
    x: &'a str,
    y: &'a str,
    ann: T,
) -> &'a str
where
    T: Display,
{
    println!("Announcement! {ann}");
    if x.len() > y.len() { x } else { y }
}