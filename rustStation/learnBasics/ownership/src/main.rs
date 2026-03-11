fn main() {
    //scope variable validity
    //string literal, hardcoded (not allocated)
    {
        // s is not valid here, since it's not yet declared
        let s = "hello"; // s is valid from this point forward
        // do stuff with s
    }

    let mut s = String::from("hello");
    s.push_str(", world!"); // push_str() appends a literal to a String
    println!("{s}"); // this will print `hello, world!`

    //scope variable validity
    //string allocated on the heap, and freed at the end of scope
    {
        let s = String::from("hello"); // s is valid from this point forward
        // do stuff with s
    } // this scope is now over, and s is no longer valid

    //types with known size : on stack
    let x = 5;
    let y = x; //copy of value of x on the stack

    //types with unknown size : on heap
    let s1 = String::from("hello");
    let s2 = s1;
    //on C : pointer on same data (memcpy is used otherwise)
    //double free (drop) error if goes out of scope!
    //so with Rust, s2 -> data of s1 & s1 goes invalid

    //assigning new data
    let mut s = String::from("hello");
    s = String::from("ahoy"); //here Rust drop (free) s & allocates the new data
    println!("{s}, world!");

    //clone pointer data (memcpy)
    let s1 = String::from("hello");
    let s2 = s1.clone(); //expensive for large size
    println!("s1 = {s1}, s2 = {s2}");
    //2 pointers that points to different data
    //if one modified, the other one doesn't

    //ownership & function params
    //types implementing copy trait (size known) are copied onto the function's stack
    let s = String::from("hello"); // s comes into scope
    takes_ownership(s); // s's value moves into the function...
    // ... and so is no longer valid here
    let x = 5; // x comes into scope
    makes_copy(x); // Because i32 implements the Copy trait,
    // x does NOT move into the function,
    // so it's okay to use x afterward.

    //ownership & function return
    let s1 = gives_ownership(); // gives_ownership moves its return
    // value into s1
    let s2 = String::from("hello"); // s2 comes into scope
    let s3 = takes_and_gives_back(s2); // s2 is moved into
    // takes_and_gives_back, which also
    // moves its return value into s3

    //ownership : param & return function
    let s1 = String::from("hello");
    let (s2, len) = calculate_length_tuple(s1); //returns ownership of s1 by returning tuple
    println!("The length of '{s2}' is {len}.");

    //without ownership : reference (borrowing)
    let s1 = String::from("hello");
    let len = calculate_length(&s1); //does not own s1
    println!("The length of '{s1}' is {len}.");
    //giving a reference "&" to function as C++
    //it is a valid pointer that points to the original pointer

    //borrowing & changes
    let mut s = String::from("hello");
    change(&mut s);

    //borrowing allowed only once for mutable
    let mut s = String::from("hello");
    {
        let r1 = &mut s;
        //let r2 = &mut s; error second borrowing not allowed!
    } // r1 goes out of scope here, so we can make a new reference with no problems.
    let r2 = &mut s;

    //borrowing allowed unlimited times for non-mutable references
    //but also no mutable reference can be made if non-mutable has been done
    let mut s = String::from("hello");
    let r1 = &s; // no problem
    let r2 = &s; // no problem
    //let r3 = &mut s; BIG PROBLEM
    println!("{r1}, {r2}");
    //end scope of reference r1 & r2 because unused after
    let r3 = &mut s; // no problem, no references
    println!("{r3}");

    //dangle error
    //let reference_to_nothing = dangle();
    let reference_to_nothing = no_dangle();

    //slices
    let mut s = String::from("hello world");
    let word = first_word_len(&s); // word will get the value 5
    s.clear(); // this empties the String, making it equal to ""
    // word still has the value 5 here, but s no longer has any content that we
    // could meaningfully use with the value 5, so word is now totally invalid!

    //slices range
    let s = String::from("hello world");
    let hello = &s[0..5];
    let world = &s[6..11];
    //reference to struct containing pointer (as &s[6] in C) with length of 5

    //borrowing & slices
    let mut s = String::from("hello world");
    let word = first_word(&s);
    //s.clear(); error! because s is already borrowed!
    println!("the first word is: {word}"); //reference end
    s.clear(); //works

    //string literal
    let s = "Hello, world!"; //type : &str, reference to binary's immutable string

    //string slice better for manipulating strings
    let my_string = String::from("hello world");
    // `first_word_slice` works on slices of `String`s, whether partial or whole.
    let word = first_word_slice(&my_string[0..6]);
    let word = first_word_slice(&my_string[..]);
    // `first_word` also works on references to `String`s, which are equivalent
    // to whole slices of `String`s.
    let word = first_word_slice(&my_string);
    let my_string_literal = "hello world";
    // `first_word` works on slices of string literals, whether partial or whole.
    let word = first_word_slice(&my_string_literal[0..6]);
    let word = first_word_slice(&my_string_literal[..]);
    // Because string literals *are* string slices already,
    // this works too, without the slice syntax!
    let word = first_word_slice(my_string_literal);

    //works with other types
    let a = [1, 2, 3, 4, 5];
    let slice = &a[1..3]; //slice type : &[i32]
    assert_eq!(slice, &[2, 3]);
}

fn takes_ownership(some_string: String) {
    // some_string comes into scope
    println!("{some_string}");
} // Here, some_string goes out of scope and `drop` is called. The backing
// memory is freed.

fn makes_copy(some_integer: i32) {
    // some_integer comes into scope
    println!("{some_integer}");
} // Here, some_integer goes out of scope. Nothing special happens.

fn gives_ownership() -> String {
    // gives_ownership will move its
    // return value into the function that calls it
    let some_string = String::from("yours"); // some_string comes into scope
    some_string // some_string is returned and
    // moves out to the calling function
}

// This function takes a String and returns a String.
fn takes_and_gives_back(a_string: String) -> String {
    // a_string comes into scope
    a_string // a_string is returned and moves out to the calling function
}

fn calculate_length_tuple(s: String) -> (String, usize) {
    let length = s.len(); // len() returns the length of a String
    (s, length)
}

//entry as reference (const)
fn calculate_length(s: &String) -> usize {
    s.len()
} // Here, s goes out of scope. But because s does not have ownership of what
// it refers to, the String is not dropped.

//entry as modifiable reference
fn change(some_string: &mut String) {
    some_string.push_str(", world");
}

/*
fn dangle() -> &String { // dangle returns a reference to a String
    let s = String::from("hello"); // s is a new String
    &s // we return a reference to the String, s
} // Here, s goes out of scope and is dropped, so its memory goes away.
  // Danger!
*/

fn no_dangle() -> String {
    let s = String::from("hello");
    s
}

//returning first word's len or string's entire len if no space found
fn first_word_len(s: &String) -> usize {
    let bytes = s.as_bytes(); //&[u8] : byte slice
    for (i, &item) in bytes.iter().enumerate() {
        if item == b' ' {
            return i;
        }
    }
    s.len()
}

//return &str : string slice of first word
fn first_word(s: &String) -> &str {
    let bytes = s.as_bytes();

    for (i, &item) in bytes.iter().enumerate() {
        if item == b' ' {
            return &s[0..i];
        }
    }

    &s[..]
}

//takes a slice as param
fn first_word_slice(s: &str) -> &str {
    let bytes = s.as_bytes();
    for (i, &item) in bytes.iter().enumerate() {
        if item == b' ' {
            return &s[0..i];
        }
    }
    &s[..]
}
