# presserator

ESP32 status LED blinky application written in C++ with ESP-IDF. The LED turns
on and off every half second.

## Development

Install ESP-IDF at `/opt/esp-idf` and install its toolchain. Install
[direnv](https://direnv.net/), enter the project directory, and approve the
environment once:

```sh
direnv allow
just target
just build
just flash
just monitor
```

The `.envrc` loads the ESP-IDF tools and sets `IDF_PATH`. If ESP-IDF is installed
elsewhere, update `IDF_PATH` in `.envrc` before running `direnv allow` again.
The chip target defaults to `esp32`. To use another ESP-IDF target, set
`IDF_TARGET` in `.envrc` (for example, `esp32s3`), run `direnv allow`, then run
`just target` before building. The status LED defaults to GPIO 2; change
`BLINKY_STATUS_LED_GPIO` in `main/main.cpp` for boards that use another pin.
For an active-low LED, set `BLINKY_STATUS_LED_ACTIVE_LEVEL` to `0`.
