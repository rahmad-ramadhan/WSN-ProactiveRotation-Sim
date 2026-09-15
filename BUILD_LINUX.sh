#!/bin/sh
# Build the simulator. Needs CMake 3.20+ and a C++20 compiler.
set -e
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
echo
echo "built: build/sim"
