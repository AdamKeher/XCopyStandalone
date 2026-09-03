"""Adds an "Upload All" task: the firmware and then the web assets, in one go.

The two halves are separate targets because they are separate images, and the
platform offers no target that sends both. That is fine when only one of them has
changed - which is usually the web assets - and a nuisance when both have, since
forgetting the second leaves new firmware driving an old interface.

Each upload reboots the ESP, so this waits between them. Over the air that is not
optional: the second upload would otherwise be offered to a device that is still
booting and reassociating, and espota would report a device that is not there
rather than one that is not ready yet. The wait is generous because the cost of
being wrong is a failed upload and the cost of being slow is fifteen seconds.

Runs through the same interpreter that is running PlatformIO, rather than
assuming a pio on PATH.
"""

Import("env")

env.AddCustomTarget(
    name="uploadall",
    dependencies=None,
    actions=[
        "$PYTHONEXE -m platformio run -e $PIOENV -t upload",
        '$PYTHONEXE -c "import time; time.sleep(15)"',
        "$PYTHONEXE -m platformio run -e $PIOENV -t uploadfs",
    ],
    title="Upload All",
    description="Firmware, then the filesystem image",
)
