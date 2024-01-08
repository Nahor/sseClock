// spell-checker:words chrono
use log;
use std::io::{Seek, SeekFrom, Write};
use std::path::PathBuf;
use std::{env, fs};
use windows::Win32::System::Console::AttachConsole;

pub const LOG_PATH: &str = "sseClock.log";
pub const LOG_PATH_BAK: &str = "sseClock.log.bak";
pub const MAX_LOG_SIZE: u64 = 10 * 1024 * 1024;

#[derive(Debug)]
pub struct SseLogger {
    level: log::LevelFilter,
    has_console: bool,
    file_path: Option<PathBuf>,
    bak_path: Option<PathBuf>,
}
impl SseLogger {
    pub fn has_console(&self) -> bool {
        self.has_console
    }
    pub fn get_level(&self) -> log::LevelFilter {
        self.level
    }
}

impl SseLogger {
    pub fn new() -> Self {
        let level = env::var("RUST_LOG")
            .ok()
            .and_then(|level_str| level_str.parse().ok())
            .unwrap_or(log::LevelFilter::Info);
        let has_console = unsafe { AttachConsole(0xFFFFFFFF).is_ok() };

        let file_path: Option<PathBuf>;
        let bak_path: Option<PathBuf>;

        if let Ok(path) = env::var("TMP") {
            file_path = Some([&path, &LOG_PATH.to_owned()].iter().collect::<PathBuf>());
            bak_path = Some(
                [&path, &LOG_PATH_BAK.to_owned()]
                    .iter()
                    .collect::<PathBuf>(),
            );
        } else {
            file_path = None;
            bak_path = None;
        }

        Self {
            level,
            has_console,
            file_path,
            bak_path,
        }
    }

    fn log_console(&self, log: &str) {
        if self.has_console {
            println!("{log}");
        }
    }

    fn log_file(&self, header: &str, log: &str) {
        let path = match &self.file_path {
            Some(path) => path,
            None => return,
        };

        // If the log file exists and is too big, move (or delete it can't be move)
        let mut file: Option<fs::File> = None;
        if let Ok(meta) = fs::metadata(path) {
            if meta.len() >= MAX_LOG_SIZE {
                if let Ok(mut file) = fs::File::options().append(true).open(path) {
                    let _ = file.write_all(format!("{header} - Log rotation\n").as_bytes());
                }
                if let Some(bak_path) = &self.bak_path {
                    let _ = fs::rename(path, bak_path);
                }
                // Create or truncate if we couldn't move it
                file = fs::File::options()
                    .write(true)
                    .truncate(true)
                    .create(true)
                    .open(path)
                    .ok();
            }
        }
        // Create or append
        file = file.or_else(|| {
            fs::File::options()
                .append(true)
                .create(true)
                .open(path)
                .ok()
        });
        let mut file = match file {
            Some(file) => file,
            None => return,
        };
        if let Ok(pos) = file.seek(SeekFrom::End(0)) {
            if pos == 0 {
                let _ = file.write_all(format!("{header} - Log start\n").as_bytes());
            }
        }
        let _ = file.write_all(format!("{}\n", log).as_bytes());
    }

    fn header(&self, record: &log::Record) -> String {
        let now = chrono::Local::now()
            .format("%Y-%m-%d %H:%M:%S.%3f")
            .to_string();
        format!("[{} - {:<5} {}]", now, record.level(), record.target())
    }
}

impl log::Log for SseLogger {
    fn enabled(&self, metadata: &log::Metadata) -> bool {
        (metadata.target() == "sse_clock" && metadata.level() <= self.level)
            || (metadata.level() <= log::Level::Warn)
    }

    fn log(&self, record: &log::Record) {
        if !self.enabled(record.metadata()) {
            return;
        }
        let header = self.header(record);
        let str = format!("{} {}", header, record.args());
        self.log_console(&str);
        self.log_file(&header, &str);
    }

    fn flush(&self) {
        let _ = std::io::stdout().flush();
    }
}
