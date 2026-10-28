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

Pair the device through your Bluetooth settings when needed. The utility reuses
an existing connection and leaves it connected on exit; disconnect it through
your Bluetooth settings whenever you choose. Ctrl-Shift-C quits immediately,
including during connection setup or a paste.

An optional Bluetooth address selects a particular device when more than one is
advertising:

```sh
printf 'Hello\n' | build/host/presserator-host AA:BB:CC:DD:EE:FF
```

The same address can be passed through the recipe:

```sh
printf 'Hello\n' | just run AA:BB:CC:DD:EE:FF
```

Use `just run --clipboard AA:BB:CC:DD:EE:FF` to send the clipboard to a
specific device. Clipboard mode uses `wl-paste` on Wayland or `xclip` on X11.

WezTerm normally reserves Ctrl-Shift-C for Copy. Add this binding to its
`config.keys` table to make it quit Presserator while keeping Copy elsewhere:

```lua
{
  key = "c",
  mods = "CTRL|SHIFT",
  action = wezterm.action_callback(function(window, pane)
    if pane:get_user_vars().presserator == "1" then
      window:perform_action(wezterm.action.SendString("\x1c"), pane)
    else
      window:perform_action(wezterm.action.CopyTo("Clipboard"), pane)
    end
  end),
},
```

The wire packet is 13 bytes: `PR`, version, 16-bit sequence number, modifier
byte, six HID key usages, and CRC-8. The text mapper uses a US keyboard layout;
non-ASCII input is rejected. The Linux host target requires BlueZ and the GLib
GIO development package.
