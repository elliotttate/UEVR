@echo off
echo Building UEVR Cutscene Detection Plugin...

if not exist "build" mkdir build
cd build

echo Configuring CMake...
cmake .. -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 (
    echo CMake configuration failed!
    exit /b 1
)

echo Building Release configuration...
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

echo Build completed successfully!
echo Plugin DLL is located at: build\Release\CutsceneDetectionPlugin.dll
echo Copy this file to your game's UEVR\plugins\ directory