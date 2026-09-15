"""PlatformIO custom target: flash the firmware AND the LittleFS image in one command.

    pio run -e esp32dev -t uploadall

Why a script: PlatformIO has no built-in combined target - `upload` (src/) and
`uploadfs` (data/) are separate builders with their own images - so almost every
change here needed two commands, and forgetting `uploadfs` silently leaves the old
web UI on the device against new firmware.

Each action re-enters `pio run`. The child process loads this script too, but only to
register the target again, so there is no recursion. $PYTHONEXE is PlatformIO's own
interpreter, which is more reliable than assuming `pio` is on PATH.

Order is firmware then filesystem: the fs upload reboots the board last, so the device
comes up running the new firmware against the new data/.
"""
Import("env")  # noqa: F821 - injected by PlatformIO/SCons

env.AddCustomTarget(  # noqa: F821
    name="uploadall",
    dependencies=None,
    actions=[
        '"$PYTHONEXE" -m platformio run -e $PIOENV -t upload',
        '"$PYTHONEXE" -m platformio run -e $PIOENV -t uploadfs',
    ],
    title="Upload all",
    description="Flash firmware (src/) + LittleFS (data/) in one go",
)
