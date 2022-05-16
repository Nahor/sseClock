# sseClock

Simple application for SteelSeries devices to show the current date and time.

## Build/Compilation

### Building with Visual Studio (only tested with Visual Studio Community 2022)

Make sure CMake support has been added to Visual Studio
Just open the folder with VS2022 and compile

### Building with [MSYS2](https://www.msys2.org/) + [Mingw-w64](https://www.mingw-w64.org/)

```bash
cd <project directory>
mkdir build
cd build
cmake ..
make
```

### Automatic startup

Add the generated application to your startup directory.

* Open Windows Explorer
* Ctrl-L (or click in the "url" bar)
* Enter `startup` then press `Enter`. This should send you to a directory like
  `C:\Users\<user>\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup`
* Copy the binary or create a shortcut.

### Stopping

* From a terminal, use `Ctrl+C`
* When launch from Windows Explorer, kill the app using the Task Manager

## Logs

In case of issues, logs are located in temporary directory:

* `C:\Users\<user>\AppData\Local\Temp\sseClock.log` when the application is
  started directly from Windows.
* MSYS2's TMP directory (typically `C:\msys64\tmp\sseClock.log`) when started
  from an MSYS2 terminal

## Notes

SteelSeries Engine is full of race conditions. This can cause some delays when
starting sseClock. In particular, at boot, it can take up to 10 min for it work
because SSE believes sseClock is spamming it after one failed attempt to
connect (`Events for too many games have been registered recently, please try
again later`).
