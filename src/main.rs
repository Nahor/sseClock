// spell-checker:words chrono
#![windows_subsystem = "windows"]

use log::{info, warn};
use once_cell::sync::Lazy;

use sse_clock::{SseClock, SseLogger};

static LOGGER: Lazy<SseLogger> = Lazy::new(|| SseLogger::new());

fn main() {
    let _ = log::set_logger(&*LOGGER).map(|()| log::set_max_level(LOGGER.get_level()));

    info!("App starting");
    info!("logger: {:?}", *LOGGER);

    let mut sse_clock = SseClock::new();
    let stop = sse_clock.get_stop_notify();

    ctrlc::set_handler(move || {
        info!("Received terminate event");
        stop.stop();
    })
    .expect("Unable to set terminate handler");

    if let Err(err) = sse_clock.run() {
        warn!("{err:?}");
    }

    info!("App done");
}
