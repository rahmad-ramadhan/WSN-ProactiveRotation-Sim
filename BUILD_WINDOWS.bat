@echo off
REM Build the simulator. Needs CMake 3.20+ and a C++20 compiler.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build --parallel || exit /b 1
echo.
echo built: build\sim.exe
