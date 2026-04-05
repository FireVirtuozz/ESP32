# Learning Rust from Rust Book

Doc : https://doc.rust-lang.org/std/prelude/index.html

Packages : https://crates.io/

## Chap 1 : Getting started

Projects: 
- hello_cargo
- HelloWorldFirst

**Without cargo**

```bash
rustc main.rs #compile
./main #run

rustfmt #format code
```

**With Cargo**

Check in Cargo.toml: package, dependencies...

```bash
cargo --version
cargo new hello_cargo

cargo build
./target/debug/hello_cargo
cargo run # build & run

cargo check # does not build exec
cargo build --release # debug by default

cargo update # update dependencies (crates)

cargo doc --open

cargo fmt #format

cargo new restaurant --lib

cargo test

RUST_BACKTRACE=1 cargo run #for backtrace when panicking

cargo build --release #for release build version, default: debug

cargo test -- --test-threads=1 #for sequential testing

cargo test one_hundred #single test
cargo test add #tests containing "add"
```

**VSCode extensions**

- Even Better TOML
- Rust Analyzer

## Chap 2 : Gessing game

Projects:
- guessing_game

Introduction to: `std::io`, `expect()`, `println()!` macro, `rand` crate dependency, `let mut`, `match`, `: u32` annotation, `loop`, invalid value `Ok()` & `Err()`...

## Chap 3 : Programming concepts

Projects:
- variables

**Variables** : Mutable `let mut` / Immutable `let`, constants `const` + annotation, shadowing

**Data types** : u8, u16, u32, u64, u128, usize (and i) / Decimal (1_000), Hex (0xff), Octal (0o77), Binary (0b1111_0000), Byte (b'A') [u8] / f32, f64 / bool, char (4 bytes, more than ASCII), tuple, array...

**Functions** : function `fn`, parameters `x: i32`, return `-> i32`...

**Control flow** : `if else if else`, `let if else`, `let loop`, `'name: loop`, `while`, `for i in (1..4)`...

## Chap 4 : Ownership

Projects:
- ownership

Variable scope: variable dropped when out of scope.

**Memory : Heap & Stack**

String type (heap): `String::from`...

`let s2 = s1` -> copy String data representation (not the content), and invalidate s1 to prevent double-free errors. 

`let mut s` and `s = ..` -> drop content and alloc new content.

`let s2 = s1.clone()` -> copy String data representation and the content, whole new variable.

**Ownership**

When variable passed in parameter of a function (`s: String`), this one is taking the ownership of the variable, thus when the function is done, it goes out of scope and the parameter is dropped. It has the ownership of the variable, so it is the original variable that it is dropped!

On the other hand, if a variable is returned from a function (`-> String`), its ownership too!

**Borrowing**

To avoid giving & returning ownership of each variable everytime in a function, we should use *references `s: &String`*. A reference is just a pointer that points to the data. So when s goes out of scope, it drops just the pointer, and not the data!

If we want to modify it, we should use `s: &mut String`. Warning: we can't make 2 mutable references.

When a function produces something, it should be just giving an ownership, not a reference because this one is dropped at the end, or at least it should be with a specified lifetime. (dangling references)

**Slice**

Portion of String: `&s[0..2]` of type `&str`-> reference associated with len (here 3), and `&str = &str[..] = String`

Literal strings are already string slices, we could pass it as a parameter without taking a reference.

It is better to work with slices everytime.

## Chap 5 : Structures

Projects:
- structures

Goes through `struct`, tuple structs, unit structs, traits `#[derive(Debug)]` with `dbg!`, methods `impl`, associated function `-> Self`...

Rust automatically dereferences when using `.` to access structs, no need to use `->`.

## Chap 6 : Enum & Pattern

Projects:
- enums

Goes through `enum`, typed enums, `match`, pattern matching with `match`, `other` and `_` are the same, except other store the match's variable.

Flow control with `ìf let` and `let ... else` *pattern matching*.

## Chap 7 : Modules

Projects:
- backyard
- restaurant

Goes through project management, with `pub` for public, `mod`, `use`, external packages such as rand. We can do some kind of inheritance with modules. A child can call private functions of a parent, because a child *is* the parent.

Keep in mind `no_std` for embedded applications.

## Chap 8 : Collections

Projects:
- collections

Goes through collections like `Vec<T>` (dynamic array), `String`, `HashMap<K, V>`. These ones are dynamic, so it is stored on the Heap.

## Chap 9 : Errors

Projects:
- errors

Error handling with `Result<T, E>`. Goes through `panic!`, `match` and `ErrorKind`, `unwrap_or_else`, `unwrap()` and `expect()` for prototypes, `?` for error propagation, `Box<dyn Error>` for any kind of error.

## Chap 10 : Generic & Lifetimes

Projects:
- generic

**Generic Data types**

Removing duplication by using generic. Generic data types can be used in functions `largest<T>(list: &[T]) -> &T`, if the type implements corresponding traits, such as `cmp`. It can also be used for types `struct Point<T>`, with `impl<T> Point<T>` for methods.

**Traits (interfaces)**

It goes through *traits* `pub trait Summary`, that are like *interfaces* in Java, and are implemented by types with `impl Summary for NewsArticle`. We could define default implementations for functions in these traits, such as an abstract class in Java. In parameters of a function, we could specify the trait that has to be implemented with `notify(item: &impl Summary)` or `notify<T: Summary>(item: &T)`, useful for the largest function seen before for instance. It is like in Java, when we filter a type by its interface. We can also return these traits, with `-> impl Summary`.

**Lifetimes**

A reference is associated with its lifetime, which is the moment where it is declared until when it goes out of scope. If it depends on others lifetimes, it takes the *minimum lifetime*, so it cannot be used after. The compiler detects this. For instance, `longest<'a>(x: &'a str, y: &'a str) -> &'a str` this function needs lifetimes specified because we can't know f the value returned will be x or y, so we have to specify a lifetime, and here it takes the minimal one. We can define lifetimes for `struct ImportantExcerpt<'a>` so when it takes a reference such as `part: &'a str,`, the struct does not live longer than the reference in it, to avoid pointing towards nothing. With this we can define methods with `impl<'a> ImportantExcerpt<'a>`. There is also the `s: &'static str`, which refers to a lifetime of the entire program, such as &str for static strings like `let s = "I have a static lifetime."`.

## Chap 11 : Tests

Projects;
- adder

We can write automated tests to confirm what our program is doing, with `#[cfg(test)]` and `#[test]` (see in *adder* for details). We can use `assert!`, `assert_eq!`, `assert_ne!`, and custom messages with `assert!`. We can check if the program is panicking according to a specific error with `#[should_panic(expected = "less than or equal to 100")]` and `panic!`. We can use `Result<T, E>` with `Err` to specify a custom error. Ignore tests using `#[ignore]`, run them with `cargo test -- --ignored`.

It also goes through Unit and Integration tests. Integration tests are only made on projects with `lib.rs` and need some file organization with `tests`, `common` directories.

## Chap 16 : Concurrency

Projects:
- concurrent
- channel
- shared

**Threads**

Goes through creating threads for parallel execution `thread::spawn(|| {})` with closure. Keep in mind that all threads are like *deamon* threads in Java, when the main program exits, it kills all threads. We have to wait that they finish their execution with `handle.join()`. If we want to pass a variable, that means its ownership, from the main to the thread, we have to use `move`. However, after the thread's creation, the main program can't use this variable because the thread owns it now.

**Messages**

Sometimes we need to send messages from a thread to another, it is often referred in other langages as a *mailbox*.

Between threads, we can communicate with a channel `let (tx, rx) = mpsc::channel();` from `mpsc` for multiple produceurs, single consumer. We send a value (from thread) with `tx.send(val)`, and the receiver (main) will wait for it with `rx.recv()`. However, when the value is sent, we can't use it anymore, it is the channel that owns this variable. We can send multiple values by looping, and we can define multiple producers (threads) but we have to clone the transmitter with `tx.clone()` because of the ownership problem.

**Shared State**

Sometimes, multiples threads want to access to the same data, but that data needs to be protected because everyone can't access it at the same time. In other langages we use a *mutex*.

We can use a mutex and its lock with `m.lock()`, but multiple threads means multiples ownerships, we can't just `move` the mutex in all the threads due to ownership problems. We have to use `Arc<T>` with `new` and `clone` to allow a shared-memory communication between threads.

If we want to define our own concurrency without the std library, we have to use `Send` and `Sync`.