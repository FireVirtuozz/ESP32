//! Error module
//! This is a module to handle several types of errors

use std::{any::Any, array::TryFromSliceError, sync::mpsc::SendError};

use crate::monitor::TelemetryPacket;

#[derive(Debug)]
pub enum AppError {
    Eframe(eframe::Error),
    Thread(Box<dyn std::any::Any + Send>),
    Udp(std::io::Error),
    Send(String),
    Slice(TryFromSliceError),
    Other(String),
}

impl std::fmt::Display for AppError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            AppError::Eframe(e) => write!(f, "eframe error: {}", e),
            AppError::Thread(e) => write!(f, "thread error: {:?}", e),
            AppError::Udp(e) => write!(f, "udp error: {}", e),
            AppError::Send(e) => write!(f, "tx send error: {}", e),
            AppError::Slice(e) => write!(f, "slice error: {}", e),
            AppError::Other(e) => write!(f, "{}", e),
        }
    }
}

impl std::error::Error for AppError {}

impl From<std::io::Error> for AppError {
    fn from(e: std::io::Error) -> Self {
        AppError::Udp(e)
    }
}

impl From<Box<dyn Any + Send>> for AppError {
    fn from(e: Box<dyn std::any::Any + Send>) -> Self {
        AppError::Thread(e)
    }
}

impl From<SendError<TelemetryPacket>> for AppError {
    fn from(e: SendError<TelemetryPacket>) -> Self {
        AppError::Send(e.to_string())
    }
}

impl From<&str> for AppError {
    fn from(e: &str) -> Self {
        AppError::Other(e.to_string())
    }
}

impl From<TryFromSliceError> for AppError {
    fn from(e: TryFromSliceError) -> Self {
        AppError::Slice(e)
    }
}