@echo off
REM Build script for TurnChanges DLL Plugin

echo =========================================
echo Building TurnChanges DLL Plugin for UEVR
echo =========================================

REM Check if we're in the right directory
if not exist "Plugin.cpp" (
    echo Error: Plugin.cpp not found. Please run this script from the plugin directory.
    pause
    exit /b 1
)

REM Create build directory
if not exist "build" mkdir build
cd build

REM Configure with CMake
echo Configuring with CMake...
cmake .. -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% neq 0 (
    echo Error: CMake configuration failed!
    pause
    exit /b 1
)

REM Build the project
echo Building plugin...
cmake --build . --config Release
if %ERRORLEVEL% neq 0 (
    echo Error: Build failed!
    pause
    exit /b 1
)

echo.
echo =========================================
echo Build completed successfully!
echo.
echo Output location: build\plugins\Release\TurnChangesPlugin.dll
echo.
echo To install:
echo 1. Copy TurnChangesPlugin.dll to your game's UEVR\plugins\ folder
echo 2. Launch your game with UEVR
echo 3. The plugin will add L3+R3 aim toggle and new DPad methods
echo =========================================

pause 