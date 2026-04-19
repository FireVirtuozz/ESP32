use std::collections::HashMap;

fn main() {
    //vector are dynamic arrays (malloc in C), on the heap
    //arrays are static arrays (a[] in C), on the stack
    let v: Vec<i32> = Vec::new();
    let v = vec![1, 2, 3]; //with initial values, using vec macro
    let mut v = Vec::new();

    //push values
    v.push(5);
    v.push(6);
    v.push(7);
    v.push(8);

    //reading values
    let v = vec![1, 2, 3, 4, 5];
    let third: &i32 = &v[2]; //panic if index out of bounds
    println!("The third element is {third}");
    //better (safe) with Option
    let third: Option<&i32> = v.get(2);
    match third {
        Some(third) => println!("The third element is {third}"),
        None => println!("There is no third element."),
    }

    //looping elements
    let v = vec![100, 32, 57];
    for i in &v {
        println!("{i}");
    }

    //looping & edit elements
    let mut v = vec![100, 32, 57];
    for i in &mut v {
        //dereferecing, println! takes a reference as param so no need for it
        *i += 50;
    }

    //multiple types in vector
    enum SpreadsheetCell {
        Int(i32),
        Float(f64),
        Text(String),
    }
    let row = vec![
        SpreadsheetCell::Int(3),
        SpreadsheetCell::Text(String::from("blue")),
        SpreadsheetCell::Float(10.12),
    ];

    //remove value : pop..
    //see here for more operations
    //https://doc.rust-lang.org/std/vec/struct.Vec.html

    //Strings
    let mut s = String::new();
    let s = "initial contents".to_string();
    let s = String::from("initial contents");

    //UTF-8 encoded
    let hello = String::from("السلام عليكم");
    let hello = String::from("Dobrý den");
    let hello = String::from("Hello");
    let hello = String::from("שלום");
    let hello = String::from("नमस्ते");
    let hello = String::from("こんにちは");
    let hello = String::from("안녕하세요");
    let hello = String::from("你好");
    let hello = String::from("Olá");
    let hello = String::from("Здравствуйте");
    let hello = String::from("Hola");

    //Q : for avionics, is there a heap? everything is predefined before right?

    //append
    let mut s = String::from("foo");
    s.push_str("bar");
    s.push('l');

    //concat
    let s1 = String::from("Hello, ");
    let s2 = String::from("world!");
    //add signature: self, &str -> String
    //&String to &str : dereference coercion, &(*s)[..]
    let s3 = s1 + &s2;
    // note s1 has been moved here and can no longer be used (takes ownership & goes out of scope)

    let s1 = String::from("tic");
    let s2 = String::from("tac");
    let s3 = String::from("toe");
    let s = format!("{s1}-{s2}-{s3}");

    let hello = "Здравствуйте";
    //let answer = &hello[0]; returns i8 value, cuz String = Vec<i8>, 208 here
    let s = &hello[0..4]; //each char is 2 bytes (short) --> 2 first chars Зд
    //panic if not pair (e.g: 0..3)

    //iterate chars
    for c in "Зд".chars() {
        println!("{c}");
    }
    //iterate bytes
    for b in "Зд".bytes() {
        println!("{b}");
    }

    //details : https://doc.rust-lang.org/stable/alloc/string/struct.String.html

    //HashMap : on heap
    let mut scores = HashMap::new();
    scores.insert(String::from("Blue"), 10);
    scores.insert(String::from("Yellow"), 50);
    let team_name = String::from("Blue");
    let score = scores.get(&team_name).copied().unwrap_or(0); //access
    //iterate
    for (key, value) in &scores {
        println!("{key}: {value}");
    }

    //ownership in HashMap
    let field_name = String::from("Favorite color");
    let field_value = String::from("Blue");
    let mut map = HashMap::new();
    map.insert(field_name, field_value);
    // field_name and field_value are invalid at this point

    //Overwrite
    scores.insert(String::from("Blue"), 10);
    scores.insert(String::from("Blue"), 25);
    println!("{scores:?}");

    let mut scores = HashMap::new();
    scores.insert(String::from("Blue"), 10);
    scores.entry(String::from("Yellow")).or_insert(50); //check if contains
    scores.entry(String::from("Blue")).or_insert(50);
    println!("{scores:?}");

    //update, based on old value
    let text = "hello world wonderful world";
    let mut map = HashMap::new();
    for word in text.split_whitespace() {
        let count = map.entry(word).or_insert(0);
        *count += 1;
    }
    println!("{map:?}");

    //Median
    let mut v = vec![100, 32, 57, -50, -20, 25];
    println!("Original vector : {v:?}");
    v.sort();
    println!("Sorted vector : {v:?}");
    let length = v.len();
    println!("Length vector : {}", length);
    let mut res_med = -1;
    if length % 2 == 0 {
        let val1 = v.get(length/ 2 - 1);
        let val2 = v.get(length/ 2);
        if val2 != None && val1 != None {
            res_med = (*val1.unwrap() + *val2.unwrap()) / 2;
        }
    } else {
        let val1 = v.get(length/ 2);
        if val1 != None {
            res_med = *val1.unwrap();
        }
    }
    println!("Median value : {res_med}");

    //version from claude
    //option --> let if, match!! not unwrap, deref
    // option for median instead of "-1"
    let median = if length % 2 == 0 {
        match (v.get(length / 2 - 1), v.get(length / 2)) {
            (Some(a), Some(b)) => Some((a + b) / 2),
            _ => None,
        }
    } else {
        v.get(length / 2).copied() //instead of *
    };
    match median {
        Some(val) => println!("Median : {val}"),
        None => println!("Empty vector"),
    }

} // <- v, map goes out of scope and is freed here
