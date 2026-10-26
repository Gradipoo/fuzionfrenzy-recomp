#!/bin/sh
# Apply the cross-platform lockstep patch. Safe to run twice. CMake runs the
# same script via tools/apply-lockstep.cmake.
set -eu
cd "$(dirname "$0")/.."
cmake -P tools/apply-lockstep.cmake
