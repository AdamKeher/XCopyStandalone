# Tests

```
pio test -e native      # the console interpreter
pio test -e native_adf  # ADFlib and the directory walk
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

`test_adf` covers ADFlib and `XCopyAdfWalk`, the streaming directory iterator the
console lists through. 14 cases: geometry detection on reopen, OFS and FFS volumes,
a file written and read back byte for byte across a remount, the directory walk
following a hash chain and descending a level, free block accounting across delete
and remount, and the failure paths - a file that is not there, a file that is not an
image, and a write to a read only volume.

It runs against the real vendored library in `src/adflib/`, with upstream's dump
driver standing in for the SD card. That driver is the same `AdfDeviceDriver` vtable
over stdio that `XCopyAdfSdDriver` implements over SdFat, so what is being checked
is the contract both have to satisfy. The one thing the library asks of its host -
the wall clock - is answered in `test_adf/adf_host.c` with a fixed date, so an image
the tests write is reproducible.

It has already earned its place. `adfFreeEntry()` frees the entry struct as well as
the strings inside it, so calling it on a stack entry frees a stack address. That is
an instant crash here and would have been silent heap corruption on the Teensy,
surfacing somewhere else entirely, hours later.

`shim/Arduino.h` is the whole environment the console suite needs: a `String` over `std::string`
with the same surface, including the places Arduino's is unusual and the firmware
relies on it — `substring()` clamps rather than throws, `remove()` truncates,
`charAt()` past the end returns 0.

## What is not here, and cannot be

Anything that needs a floppy drive, an SD card, the TFT or the ESP. That is most of
the firmware, and it is still checked the only way it can be: by building it and
running it on the device. `XCopyAdfSdDriver` and the live floppy driver are in that
group; what stands in for them is the dump driver, which implements the same
interface.

## Adding a suite

One directory per suite under `test/`, named `test_*`, holding a Unity test file
with its own `main()`. If it needs more of the firmware than `String`, the honest
move is usually to give the code under test a seam — as `XCopyConsoleIO.h` did —
rather than to grow the shim until it is a second Arduino.
