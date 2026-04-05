pub fn add(left: u64, right: u64) -> u64 {
    left + right
}

pub fn add_two(a: u64) -> u64 {
    a + 2
}

pub fn greeting(name: &str) -> String {
    String::from("Hello!")
}


#[cfg(test)]
mod tests {
    use super::*;

    //test that works, with equality
    //for inequality, use assert_ne!
    #[test]
    fn it_works() {
        let result = add(2, 2);
        assert_eq!(result, 4);
    }

    //test that fails
    #[test]
    #[ignore]
    fn it_doesnt_works() {
        let result = add(2, 2);
        assert_eq!(result, 5);
    }

    #[test]
    #[ignore]
    fn another() {
        panic!("Make this test fail");
    }

    #[test]
    #[ignore]
    fn it_adds_two() {
        let result = add_two(2);
        assert_eq!(result, 4);
    }

    //detailed error when testing
    #[test]
    #[ignore]
    fn greeting_contains_name() {
        let result = greeting("Carol");
        assert!(
            result.contains("Carol"),
            "Greeting did not contain name, value was `{result}`"
        );
    }

    //test with result, not panicking, convenient with ? error propagation
    #[test]
    fn it_works_result() -> Result<(), String> {
        let result = add(2, 2);

        if result == 4 {
            Ok(())
        } else {
            Err(String::from("two plus two does not equal four"))
        }
    }
}

//command to run tests: cargo test