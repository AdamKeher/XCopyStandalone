"""Make the "Upload Filesystem Image OTA" task actually send a filesystem image.

The ESP8266 platform builder decides whether espota is sending a sketch or a
filesystem image with an exact string match:

    if "uploadfs" in COMMAND_LINE_TARGETS:
        env.Append(UPLOADERFLAGS=["-s"])

COMMAND_LINE_TARGETS holds whole target names, so for the uploadfsota target that
test is "uploadfs" in ["uploadfsota"], which is False. The flag is not added, and
espota offers the filesystem image to the device as a sketch.

The device then does exactly the right thing with the wrong question. A sketch is
staged into free flash and copied into place on reboot, so it has to fit the
sketch region - 0xfeff0, about 1MB. Our filesystem image is 2MB, and the upload
dies at the invitation with:

    Bad Answer: ERR: ERROR[4]: Not Enough Space

which reads like the device is out of room when the image would have fitted its
partition exactly, had it been offered as one.

Both tasks appear in the IDE and the OTA one is the obvious thing to click for an
OTA upload, so this adds the flag the platform misses rather than leaving a
task that cannot succeed. uploadfs is unaffected: the platform has already added
the flag by the time this runs, and espota takes it once.
"""

from SCons.Script import COMMAND_LINE_TARGETS

Import("env")

if "uploadfsota" in COMMAND_LINE_TARGETS and env.subst("$UPLOAD_PROTOCOL") == "espota":
    flags = env.get("UPLOADERFLAGS", [])
    if "-s" not in flags and "--spiffs" not in flags:
        env.Append(UPLOADERFLAGS=["-s"])
        print("espota: added -s, sending a filesystem image rather than a sketch")
