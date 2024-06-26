// See comment about `clippy::large_enum_variant` is error.rs
#![allow(clippy::result_large_err)]
mod error;
mod logger;
mod sse_json;

pub use logger::*;

use chrono::prelude::*;
use error::*;
use log::{debug, error, info, warn};
use notify::{RecursiveMode, Watcher};
use serde::Serialize;
use sse_json::*;
use std::{
    env, fs,
    path::PathBuf,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Condvar, Mutex,
    },
    time::{Duration, SystemTime, UNIX_EPOCH},
};

const SSE_PROP_PATH: &str = "SteelSeries/SteelSeries Engine 3/coreProps.json";
const SSE_APP_ID: &str = "CLOCK_DISPLAY";
const SSE_DISPLAY_NAME: &str = "Clock Display";
const SSE_EVENT_ID: &str = "CLOCK";

// Binding the event can take a long time (I saw 2s once). Moreover, this
// timeout does not apply for establishing the TCP connection, which would be
// the most useful.
const HTTP_TIMEOUT: Duration = Duration::from_secs(5);
const MIN_RETRY_DELAY: Duration = Duration::from_secs(1);
const MAX_RETRY_DELAY: Duration = Duration::from_secs(5 * 60);
const MIN_ADDRESS_AGE: Duration = Duration::from_secs(3);

enum State {
    StartDelay,  // The SSE config file just got updated, wait for SSE to stabilize
    Registering, // Connecting to SSE
    Updating,    // Sending a new event to SSE
    Waiting,     // Waiting until the next event
    ErrorDelay,  // Waiting after an error
}

pub struct StopNotify {
    notify: Arc<(Mutex<bool>, Condvar)>,
    stopping: Arc<AtomicBool>,
}
impl StopNotify {
    pub fn stop(&self) {
        self.stopping.store(true, Ordering::Relaxed);
        self.notify();
    }

    pub fn notify(&self) {
        let (mutex, cond) = &*self.notify;
        *mutex.lock().unwrap() = true;
        cond.notify_all();
    }
}

pub struct SseClock {
    notify: Arc<(Mutex<bool>, Condvar)>,
    address: String,
    stopping: Arc<AtomicBool>,
}
impl SseClock {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn get_stop_notify(&self) -> StopNotify {
        StopNotify {
            notify: Arc::clone(&self.notify),
            stopping: Arc::clone(&self.stopping),
        }
    }

    pub fn stop(&self) {
        self.stopping.store(true, Ordering::Relaxed);
    }

    pub fn notify(&self) {
        let (mutex, cond) = &*self.notify;
        *mutex.lock().unwrap() = true;
        cond.notify_all();
    }

    fn get_sse_config_path(&self) -> SSEResult<PathBuf> {
        let program_data: PathBuf = [env::var("ProgramData")?, SSE_PROP_PATH.to_owned()]
            .iter()
            .collect();
        Ok(program_data)
    }

    fn sse_config_age(&self) -> SSEResult<SystemTime> {
        Ok(self.get_sse_config_path()?.metadata()?.modified()?)
    }

    fn get_sse_address(&mut self) -> SSEResult<&str> {
        let file = fs::File::open(self.get_sse_config_path()?)?;
        let config: serde_json::Value = serde_json::from_reader(file)?;
        let addr = config
            .get("address")
            .ok_or(SSEError::NoAddress)?
            .as_str()
            .ok_or(SSEError::NoStringAddress)?;

        if self.address != addr {
            addr.clone_into(&mut self.address);
            info!("Using address: {}", self.address);
        }
        Ok(&self.address)
    }

    fn send_request<Req>(&mut self, path: &str, body: Req) -> SSEResult<String>
    where
        Req: Serialize,
    {
        debug!("Request: {} {}", path, serde_json::to_string(&body)?);
        let address = format!("http://{}{}", self.get_sse_address()?, path);
        let body = ureq::post(address.as_str())
            .timeout(HTTP_TIMEOUT)
            .send_json(body)?
            .into_string()?;
        debug!("Response: {}", body);
        Ok(body)
    }

    fn send_metadata(&mut self) -> SSEResult<()> {
        let data = SSEGameMetadataRequest {
            game: SSE_APP_ID,
            game_display_name: SSE_DISPLAY_NAME,
            icon_color_id: 6,
        };
        self.send_request("/game_metadata", data)?;
        Ok(())
    }

    fn send_remove(&mut self) -> SSEResult<()> {
        let data = SSERemoveGameRequest { game: SSE_APP_ID };

        self.send_request("/remove_game", data)?;
        Ok(())
    }

    fn send_bind_event(&mut self) -> SSEResult<()> {
        let data = SSEBindEventRequest {
            game: SSE_APP_ID,
            event: SSE_EVENT_ID,
            handlers: vec![SSEEventHandler {
                device_type: "screened",
                zone: "one",
                mode: "screen",
                datas: vec![SSEFrameData {
                    icon_id: 15,
                    lines: vec![
                        SSELineData {
                            has_text: true,
                            context_frame_key: "date",
                        },
                        SSELineData {
                            has_text: true,
                            context_frame_key: "time",
                        },
                    ],
                }],
            }],
        };
        self.send_request("/bind_game_event", data)?;

        Ok(())
    }

    fn send_registration(&mut self) -> SSEResult<()> {
        // Remove the game (ignore the 400 error since it usually means that
        // we are not registered yet). Fail with the other errors though to
        // avoid wasting time trying to send the metadata
        let response = self.send_remove();
        match response {
            Ok(_) => Ok(()),
            Err(SSEError::HttpError(ureq::Error::Status(400, _))) => Ok(()),
            Err(err) => Err(err),
        }?;

        self.send_metadata()?;
        self.send_bind_event()?;

        Ok(())
    }

    fn send_event(&mut self) -> SSEResult<()> {
        let now = Local::now();
        let date = now.format("%Y-%m-%d").to_string();
        let time = now.format("%H:%M:%S").to_string();
        let data = SSEGameEventRequest {
            game: SSE_APP_ID,
            event: SSE_EVENT_ID,
            data: SSEEventData {
                value: &time,
                frame: SSEEventFrame {
                    date: &date,
                    time: &time,
                },
            },
        };
        self.send_request("/game_event", data)?;

        Ok(())
    }

    fn wait(&self, dur: Duration) {
        let (mutex, cond) = &*self.notify;
        let notify = mutex.lock().unwrap();
        let _ = cond
            .wait_timeout_while(notify, dur, |notify| {
                if *notify {
                    *notify = false; // auto-reset
                    false
                } else {
                    true
                }
            })
            .unwrap();
    }

    pub fn run(&mut self) -> Result<(), SSEError> {
        // Install filesystem watcher/notify to detect when the config file changes
        let notify = self.get_stop_notify();
        let mut watcher = notify::recommended_watcher(move |event| match event {
            Ok(event) => {
                debug!("File watch event: {:?}", event);
                notify.notify();
            }
            Err(err) => error!("File watch error: {:?}", err),
        })?;
        watcher.watch(
            self.get_sse_config_path()?
                .parent()
                .expect("No directory for SSE config"),
            RecursiveMode::Recursive,
        )?;

        // Main loop
        let mut state = State::StartDelay;
        let mut error_delay = Duration::default();
        while !self.stopping.load(Ordering::Relaxed) {
            state = match state {
                State::StartDelay => match self.sse_config_age() {
                    Ok(age) => {
                        let mut age = age.duration_since(UNIX_EPOCH)?;
                        let now = SystemTime::now().duration_since(UNIX_EPOCH)?;
                        if age > now {
                            // Avoid a last-modified date in the future to avoid
                            // blocking the app for a possibly long time
                            warn!("SSE config file is in the future");
                            age = Duration::default();
                        }
                        let expiration = age + MIN_ADDRESS_AGE;
                        if now < expiration {
                            self.wait(expiration - now);
                            State::StartDelay
                        } else {
                            match self.get_sse_address() {
                                Ok(_) => State::Registering,
                                Err(err) => {
                                    warn!("{err:?}");
                                    State::ErrorDelay
                                }
                            }
                        }
                    }
                    Err(err) => {
                        warn!("{err:?}");
                        State::ErrorDelay
                    }
                },
                State::Registering => match self.send_registration() {
                    Ok(_) => {
                        info!("Registration complete");
                        State::Updating
                    }
                    Err(err) => {
                        warn!("{err:?}");
                        State::ErrorDelay
                    }
                },
                State::Updating => match self.send_event() {
                    Ok(_) => State::Waiting,
                    Err(err) => {
                        warn!("{err:?}");
                        State::ErrorDelay
                    }
                },
                State::Waiting => {
                    error_delay = Duration::default();
                    let fraction = SystemTime::now().duration_since(UNIX_EPOCH)?.subsec_nanos();
                    let delay = Duration::from_nanos(1_000_000_000 - fraction as u64);
                    self.wait(delay);
                    State::Updating
                }
                State::ErrorDelay => {
                    error_delay = (error_delay * 2).clamp(MIN_RETRY_DELAY, MAX_RETRY_DELAY);
                    info!("Delaying by {:?}", error_delay);
                    self.wait(error_delay);
                    State::StartDelay
                }
            };
        }

        // Try to unregister if we are registered
        if let State::Updating | State::Waiting = state {
            // We are quitting anyway, so ignore any error
            let _ = self.send_remove();
        }

        Ok(())
    }
}

impl Default for SseClock {
    fn default() -> Self {
        SseClock {
            notify: Arc::new((Mutex::new(false), Condvar::new())),
            address: String::default(),
            stopping: Arc::new(AtomicBool::new(false)),
        }
    }
}
