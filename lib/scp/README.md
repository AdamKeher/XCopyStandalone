# scp - SuperCard Pro image format

The on-disk layout of a SuperCard Pro (`.scp`) flux image, vendored so that XCopy can
write flux-level images that open in HxC, greaseweazle and FluxEngine.

## Provenance

The format layer is copied from **[keirf/Disk-Utilities](https://github.com/keirf/Disk-Utilities)**
at commit `5e690f3aa248c4d66d2b233ba3e6f7147fbd0311`, which is released into the **public
domain** under the Unlicense (see `LICENSE`, copied verbatim from that repository's
`COPYING`). Public domain is compatible with this project's GPLv3.

Copied verbatim from `libdisk/container/scp.c`:

- `struct disk_header`  -> `scp_disk_header`
- `struct track_header` -> `scp_track_header`
- `struct footer`       -> `scp_footer`
- the `_FLAG_*` bit numbers and `DISKTYPE_amiga`
- `SCK_NS_PER_TICK`
- the 16-bit overflow emission from `emit()`
- the byte-sum checksum from `checksum_and_write()`

Copied verbatim from `libdisk/stream/supercard_scp.c`:

- the track offset table lookup (`hdr_offset = 0x10 + tracknr * sizeof(uint32_t)`)
- the `0x0000` overflow accumulation from `scp_next_flux()`

The structs were renamed with an `scp_` prefix because `disk_header` and `track_header`
are too generic to sit alongside `XCopyDisk`, and `struct footer` would collide outright.
Nothing else about them changed - field order, types and therefore the byte layout are
untouched.

## What is *not* vendored

Everything else. Upstream is a host-side library: it holds a whole disk in memory, calls
`memalloc`/`memfree`, allocates a 1MB scratch buffer per image, and writes through POSIX
`lseek`/`write`. None of that survives contact with a Teensy 3.2 that has 64KB of RAM in
total and streams a track to SD as it is captured.

So `src/XCopy/XCopySCP.{h,cpp}` is original: a forward-only, malloc-free writer that
converts raw FTM0 timer ticks to 25ns units, emits big-endian samples into a 512-byte
block, and seeks back only to patch fixed-size headers. Upstream's `emit()` weak-bit
synthesis is also not vendored - it exists to *fabricate* plausible flux for a decoded
image, and XCopy is capturing real flux, where fabricating anything would be a lie.

Divergence is therefore total below the format layer and nil within it. A future upstream
diff is only meaningful for `SCPFormat.h`.

## Specification

The authoritative reference is the
[SuperCard Pro Image File Specification v2.5](https://www.cbmstuff.com/downloads/scp/scp_image_specs.txt)
(cbmstuff, 11 February 2024). Constants that upstream does not define - the `resolution`
byte, the extended-mode flags, the footer string encoding - come from there, and are
marked as such in `SCPFormat.h`.

The one detail worth repeating because it is so easy to get wrong: **every longword in the
file is little-endian except the flux samples, which are big-endian.**
