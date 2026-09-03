# Tests

```
pio test -e native
```

Runs on the host, not on the device. `pio run` is unaffected — it builds the two
firmwares and nothing else, because `default_envs` in `platformio.ini` names them.

## What is here

`test_cli` covers the console interpreter: the command table, the argument parser,
tab completion and the line editor. 28 cases over every shape a command line can
take, every error path, and the corners of the completer and the editor.

It runs against the real sources in `src/XCopy/`, not against copies. That is
possible because those four files depend on nothing but Arduino's `String` — the
terminal and the SD card reach them as function pointers, which is what
`src/XCopy/XCopyConsoleIO.h` exists for. The tests supply a capture buffer for one
and a fixed fake directory tree for the other.

`shim/Arduino.h` is the whole environment they need: a `String` over `std::string`
with the same surface, including the places Arduino's is unusual and the firmware
relies on it — `substring()` clamps rather than throws, `remove()` truncates,
`charAt()` past the end returns 0.

## What is not here, and cannot be

Anything that needs a floppy drive, an SD card, the TFT or the ESP. That is most of
the firmware, and it is still checked the only way it can be: by building it and
running it on the device.

## Adding a suite

One directory per suite under `test/`, named `test_*`, holding a Unity test file
with its own `main()`. If it needs more of the firmware than `String`, the honest
move is usually to give the code under test a seam — as `XCopyConsoleIO.h` did —
rather than to grow the shim until it is a second Arduino.
