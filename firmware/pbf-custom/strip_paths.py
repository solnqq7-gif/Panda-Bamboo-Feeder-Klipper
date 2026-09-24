# PlatformIO pre-build script: the Arduino core's log macros embed __FILE__, which would put the
# builder's home directory (and so their user name) into firmware.bin. Map those prefixes away.
Import("env")  # noqa: F821 - provided by PlatformIO
import os

home = os.path.expanduser("~")
project = env.subst("$PROJECT_DIR")  # noqa: F821
for path, repl in ((home, "~"), (project, ".")):
    for variant in {path, path.replace("\\", "/")}:
        env.Append(CCFLAGS=["-fmacro-prefix-map=%s=%s" % (variant, repl)])  # noqa: F821
