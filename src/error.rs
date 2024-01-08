// spell-checker:words thiserror ureq
use std::time;
use thiserror::Error;

pub type SSEResult<T> = Result<T, SSEError>;

#[derive(Error, Debug)]
pub enum SSEError {
    #[error("No ProgramData environment variable")]
    NoProgramData(#[from] std::env::VarError),
    #[error("Io error")]
    IoError(#[from] std::io::Error),
    #[error("JSON error")]
    JSonError(#[from] serde_json::Error),
    #[error("No address in SSE config")]
    NoAddress,
    #[error("HTTP error")]
    HttpError(#[from] ureq::Error),
    #[error("Time error")]
    TimeError(#[from] time::SystemTimeError),
    #[error("Notify error")]
    NotifyError(#[from] notify::Error),
    #[error("Address is not a string")]
    NoStringAddress,
    #[error("Unexpected SSE response")]
    UnexpectedSSEResponse,
    #[error("Unknown error")]
    Unknown,
}
