# presserator

An ESP32-S3 USB keyboard bridge. A Linux host utility sends typed ASCII text to
the ESP32 over Bluetooth LE, and the ESP32 presents it to the target computer
as a USB HID keyboard. The status LED continues to blink every half second.

## Development

Install ESP-IDF at `/opt/esp-idf`
Install [direnv](https://direnv.net/)

```sh
direnv allow
just target
just build
just flash
just monitor
```

## Host text sender

Build the Linux host utility with:

```sh
just host
```

To build and run it in one step, pipe text into `just run`:

```sh
printf 'Hello, world!\n' | just run
```

It reads ASCII text from standard input, discovers and pairs with the Presserator
over Bluetooth LE, then writes binary key-state protocol packets to its GATT
service. Each character becomes a HID key press followed by a release. For
example:

```sh
printf 'Hello, world!\n' | build/host/presserator-host
```

An optional Bluetooth address selects a particular device when more than one is
advertising:

```sh
printf 'Hello\n' | build/host/presserator-host AA:BB:CC:DD:EE:FF
```

The same address can be passed through the recipe:

```sh
printf 'Hello\n' | just run AA:BB:CC:DD:EE:FF
```

The wire packet is 13 bytes: `PR`, version, 16-bit sequence number, modifier
byte, six HID key usages, and CRC-8. The text mapper uses a US keyboard layout;
non-ASCII input is rejected. The Linux host target requires BlueZ and the GLib
GIO development package.
