
//trait Summary (~interface)
pub trait Summary {
    fn summarize(&self) -> String;
}

pub struct NewsArticle {
    pub headline: String,
    pub location: String,
    pub author: String,
    pub content: String,
}

//implement Summary trait functions
impl Summary for NewsArticle {
    fn summarize(&self) -> String {
        format!("{}, by {} ({})", self.headline, self.author, self.location)
    }
}

pub struct SocialPost {
    pub username: String,
    pub content: String,
    pub reply: bool,
    pub repost: bool,
}

impl Summary for SocialPost {
    fn summarize(&self) -> String {
        format!("{}: {}", self.username, self.content)
    }
}

//trait with default implementation (as AbstractClass in Java..)
pub trait SummaryDefault {
    fn summarize_default(&self) -> String {
        String::from("(Read more...)")
    }
}

//implement SummaryDefault trait functions
impl SummaryDefault for NewsArticle {}