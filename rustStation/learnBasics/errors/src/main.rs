use std::fs::File;
use std::io::ErrorKind;
use std::io::{self, Read};

//impl From<io::Error> for OurError;

fn main() -> Result<(), Box<dyn Error>> {
    //panic!("crash and burn");

    //command for backtrace (powershell) [only in debug]
    //$env:RUST_BACKTRACE=1; cargo run

    //let v = vec![1, 2, 3];
    //panics here, out of bounds
    //v[99];

    let greeting_file_result = File::open("hello.txt");
    let greeting_file = match greeting_file_result {
        Ok(file) => file,
        Err(error) => match error.kind() {
            ErrorKind::NotFound => match File::create("hello.txt") {
                Ok(fc) => fc,
                Err(e) => panic!("Problem creating the file: {e:?}"),
            },
            _ => {
                panic!("Problem opening the file: {error:?}");
            }
        },
    };

    //alternative to nested match
    let greeting_file_alternative = File::open("hello_alt.txt").unwrap_or_else(|error| {
        if error.kind() == ErrorKind::NotFound {
            File::create("hello_alt.txt").unwrap_or_else(|error| {
                panic!("Problem creating the file: {error:?}");
            })
        } else {
            panic!("Problem opening the file: {error:?}");
        }
    });

    //unwrap, does the same thing as match (return file & panics!)
    //let greeting_file_unwrap = File::open("hello_unwrap.txt").unwrap();

    //expect : same thing as unwrap, but with a message
    //let greeting_file_expect = File::open("hello_expect.txt")
    //    .expect("hello.txt should be included in this project");

    //? operator
    //only in functions where return type is Result (or Option, types impl FromResidual)
    //let greeting_file = File::open("hello.txt")?; 

    //but if return type is changed, it works
    Ok(()) //return 0
    //Box<dyn Error> meaning any kind of error
    //main can return std::process::Termination trait
}


fn read_username_from_file() -> Result<String, io::Error> {
    let username_file_result = File::open("hello_user.txt");

    let mut username_file = match username_file_result {
        Ok(file) => file,
        Err(e) => return Err(e),
    };

    let mut username = String::new();

    match username_file.read_to_string(&mut username) {
        Ok(_) => Ok(username),
        Err(e) => Err(e),
    }
}

fn read_username_from_file_alt() -> Result<String, io::Error> {
    //? is same as match in the other function
    //? calls "from"
    let mut username = String::new();
    File::open("hello.txt")?.read_to_string(&mut username)?;
    Ok(username)
}

fn read_username_from_file_alt2() -> Result<String, io::Error> {
    fs::read_to_string("hello.txt") //does everything
}

fn last_char_of_first_line(text: &str) -> Option<char> {
    text.lines().next()?.chars().last()
}