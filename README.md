# sseClock

[![Build status](https://github.com/Nahor/sseClock/actions/workflows/build.yml/badge.svg)](https://github.com/Nahor/sseClock/actions/workflows/build.yml)

Simple application for SteelSeries with OLED/LED display to show the current
date and time.
![Screenshot](/img/screenshot.jpg)

## Build/Compilation

*[**Note**: For the old C++ version, see the [main_cpp](https://github.com/Nahor/sseClock/tree/main_cpp) branch]*
### Building

```bash
cd <project directory>
cargo build -r
```

### Automatic startup

Add the generated application to your startup directory.

* Open Windows Explorer
* Ctrl-L (or click in the "url" bar)
* Enter `startup` then press `Enter`. This should send you to a directory like
  `C:\Users\<user>\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup`
* Copy the binary or create a shortcut.

### Stopping

* Use the system tray icon:  \
  ![system tray icon](/img/tray.png)
* When started from a terminal, you can also use `Ctrl+C`

## Logs

In case of issues, logs are located in the temporary directory:

* `C:\Users\<user>\AppData\Local\Temp\sseClock.log` when the application is
  started directly from Windows.
* MSYS2's TMP directory (typically `C:\msys64\tmp\sseClock.log`) when started
  from an MSYS2 terminal

## Notes

~~SteelSeries Engine is full of race conditions. This can cause some delays when
starting sseClock. In particular, at boot, it can take up to 5 min for it work
because SSE believes sseClock is spamming it after one failed attempt to
connect (`Events for too many games have been registered recently, please try
again later`). The SteelSeries people are [aware of it](https://github.com/SteelSeries/gamesense-sdk/issues/124).~~
_[Fixed as of SteelSeries Engine 20.0.0]_
