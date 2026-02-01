@echo off
cd /d "%~dp0"
echo Building XaiClient Overlay...
if not exist build mkdir build
cd build
cmake ..
if %errorlevel% neq 0 (
    echo CMake configuration failed!
    pause
    exit /b %errorlevel%
)
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b %errorlevel%
)
echo Build successful! Executable is in Overlay/build/Release/XaiOverlay.exe
pause
