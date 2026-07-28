import os
from SCons.Script import DefaultEnvironment

Import("env")

COMMAND_TARGETS = list(map(str, COMMAND_LINE_TARGETS))

def auto_buildfs(source, target, env):
    print("\n--- Build LittleFS ---")
    env.Execute('pio run -e %s --target buildfs' % env["PIOENV"])


def auto_uploadfs(source, target, env):
    print("\n--- Upload LittleFS ---")
    env.Execute('pio run -e %s --target uploadfs' % env["PIOENV"])

if "upload" in COMMAND_TARGETS:
    env.AddPostAction("upload", auto_uploadfs)

elif not COMMAND_TARGETS or "buildprog" in COMMAND_TARGETS:
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", auto_buildfs)
