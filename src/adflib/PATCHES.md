# Vendored ADFlib

Upstream: <https://github.com/lclevy/ADFlib>, tag **v0.10.7** (15 April 2026).
The tree under `src/adflib/` is `src/*.c` and `src/*.h` from that tag, unchanged
except for what is listed below. The platform subdirectories (`generic/`, `linux/`,
`win32/`) are not vendored - their only content is a native block-device driver for
a host OS, and this project supplies its own drivers instead.

Keeping the list this short is the point. Everything else that this firmware needs
ADFlib to do differently is done from outside it, through the driver interface
(`adf_dev_driver.h`) and the log callbacks (`adfEnvSetFct`), which is what the 0.10
API exists for. The previous vendored copy - 0.7.11a, from 2007 - had accumulated
around 560 changed lines because neither of those seams existed yet. Do not start a
second collection: if something here needs changing, look first for the seam.

## Upgrading

1. Take `src/*.c` and `src/*.h` from the new tag.
2. Re-apply the one patch below.
3. Keep `config.h`, this file, and check `build_src_filter` in `platformio.ini` still
   names every file that must not be compiled for the Teensy.
4. `pio test -e native` proves the library still works before any hardware does.

## Additions

### `config.h` - new file

`adf_util.h` includes `config.h` unless `BUILDING_WITH_CMAKE` is defined; upstream
generates it with autotools. Neither build here uses either, so it is written by
hand. Three macros, all naming functions both toolchains provide.

## Patches

### `adf_util.c`, `adf_util.h` - the wall clock

`adfGiveCurrentTime()` upstream calls `time()` and `localtime()`. There is no wall
clock in newlib on a Teensy - the time of day lives in TimeLib, a C++ Arduino
library - and including it here would make the whole of ADFlib depend on the Arduino
core, which would also stop it building for the `native` test environment.

The body now calls `adfHostGiveCurrentTime()`, declared in `adf_util.h` and defined
by the host:

| Environment | Definition | Behaviour |
|---|---|---|
| `teensy31` | `src/XCopy/XCopyAdfHost.cpp` | TimeLib `now()` / `breakTime()` |
| `native` | `test/test_adf/adf_host.c` | a fixed date, so a written image compares byte for byte |

`#include <time.h>` was removed with it. Both edits are marked `XCOPY PATCH` in the
source.

### `adf_dev.h` - `class` is a C++ keyword

`struct AdfDevice` has a member called `class`, so the header cannot be included
from a C++ translation unit - which `adflib.h`, wrapping itself in `extern "C"`,
clearly expects to be. The member is now declared twice behind `#ifdef __cplusplus`:
`devClass` to C++, `class` to C. Same type, same offset, so it is one field with two
spellings and the struct layout is identical in both languages. Library code goes on
saying `dev->class`; this firmware says `dev->devClass`.

Renaming it properly would be a 26-line patch across eight files. Worth reporting
upstream; a one-hunk workaround until then.

### `adf_dev_type.c` - the media table belongs in flash

`adfDevMedia[]` is `static` where it should be `static const`. Every member of it is
already `const` and nothing writes it, but without `const` on the array it lands in
`.data`: 644 bytes copied into RAM at boot and never touched again. That is an
eighth of this board's entire malloc arena. One word.

## Files excluded from the Teensy build

Vendored but filtered out in `platformio.ini`, so the tree stays a clean copy of the
tag while the firmware only pays for what it uses.

| File | Why |
|---|---|
| `adflib.c` | `adfLibInit()` registers the two drivers below and runs `checkInternals()`. `XCopyAdfHost::begin()` does the registration for the drivers that exist on this board, and the internal checks are `static_assert`s in `XCopyAdfHost.cpp`, so they fail at build time rather than at run time. |
| `adf_dev_driver_dump.c` | An ADF-file driver built on stdio `FILE`. `XCopyAdfSdDriver` is the same driver over SdFat. This is the only file in the library that touches stdio. |
| `adf_dev_driver_ramdisk.c` | An 880 KB image in RAM, on a part with 64 KB. |
| `adf_debug.c` | `backtrace()`, which needs glibc. |

All four are compiled in the `native` environment, where they cost nothing and the
dump driver is what the tests read and write images through.
