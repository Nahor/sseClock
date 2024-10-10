#![windows_subsystem = "windows"]

use std::sync::mpsc::{self, Receiver, SyncSender};

use log::{info, warn};
use sse_clock::{
    logger::SseLogger,
    see_clock::{SseClock, StopNotify},
};
use tray_item::{IconSource, TrayItem};

enum Message {
    Exit,
    Done,
}
fn main() {
    let logger = Box::new(SseLogger::new());
    let logger_info = format!("{logger:?}").to_string();
    log::set_max_level(logger.get_level());
    let _ = log::set_boxed_logger(logger);

    info!("App starting");
    info!("logger: {}", logger_info);

    let (tx, rx) = mpsc::sync_channel(1);
    let tx_sse_loop = tx.clone();

    let sse_clock = SseClock::new();
    let ctrlc_stop = sse_clock.get_stop_notify();
    let tray_stop = sse_clock.get_stop_notify();

    ctrlc::set_handler(move || {
        info!("Received Ctrl-C event");
        // Notify the SSE clock thread to stop
        ctrlc_stop.stop();
    })
    .expect("Unable to set terminate handler");

    let tray_thread = std::thread::spawn(|| tray_loop(tx, rx, tray_stop));

    sse_loop(sse_clock, tx_sse_loop);

    tray_thread.join().expect("Failed to join tray thread");

    info!("App done");
}

fn tray_loop(tx: SyncSender<Message>, rx: Receiver<Message>, tray_stop: StopNotify) {
    let mut tray = TrayItem::new("SSE Clock", IconSource::Resource("exe-icon"))
        .expect("Failed to create tray item");
    tray.add_label("SSE Clock")
        .expect("Failed to add menu label");
    tray.inner_mut()
        .add_separator()
        .expect("Failed to add menu separator");

    tray.add_menu_item("Exit", move || {
        tx.send(Message::Exit)
            .expect("Failed to send Exit message from menu");
    })
    .expect("Failed to create Exit menu item");

    while let Ok(msg) = rx.recv() {
        match msg {
            Message::Exit => {
                info!("Received menu Exit event");
                // Notify the SSE clock thread to stop
                tray_stop.stop();
            }

            Message::Done => break, // SSE clock thread is done => terminate tray thread
        }
    }
}

fn sse_loop(mut sse_clock: SseClock, tx: SyncSender<Message>) {
    if let Err(err) = sse_clock.run() {
        warn!("{err:?}");
    }

    // Notify the tray thread to stop
    tx.send(Message::Done)
        .expect("Failed to send Exit message from SSE loop");
}
