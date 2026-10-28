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

Run it interactively to send each typed key immediately:

```sh
just run
```

Press Ctrl-Shift-V to send the host clipboard through the terminal, or
Ctrl-Shift-C to quit. On WezTerm, Ctrl-Shift-C needs the Presserator-specific
key binding described below. To send the clipboard once and exit:

```sh
just run --clipboard
```

Interactive mode forwards Ctrl-letter combinations (including Ctrl-C and Ctrl-Z),
Alt combinations, arrows, Home/End, Insert/Delete, Page Up/Down, Tab/Shift-Tab,
Escape, and F1–F12. Modified navigation and function keys retain their modifiers.
Unknown terminal key sequences are ignored without ending the session.

To send text from a pipe or file instead:

```sh
printf 'Hello, world!\n' | just run
```

It reads ASCII text from the terminal or standard input, discovers and connects
to the Presserator over Bluetooth LE, then writes binary key-state protocol
packets to its GATT service. Each character becomes a HID key press followed by
a release. For example:

```sh
printf 'Hello, world!\n' | build/host/presserator-host
```
