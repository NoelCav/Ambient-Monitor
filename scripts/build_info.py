# PlatformIO pre-build script: write src/build_info.h with the build time.
# __DATE__/__TIME__ are per source file, and PlatformIO only recompiles
# changed files, so files compiled at different times disagreed. This header
# changes on every build, so its single includer (health.cpp) always
# recompiles and buildTime() is the one true stamp. (Generated; gitignored.)
import os
import time

Import("env")  # noqa: F821 (provided by PlatformIO)

path = os.path.join(env["PROJECT_SRC_DIR"], "build_info.h")  # noqa: F821
with open(path, "w") as f:
    f.write('#pragma once\n#define BUILD_TIME "%s"\n' % time.strftime("%Y-%m-%d %H:%M"))
