@echo off
echo Building ButtonRemapperPlugin...

if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    exit /b 1
)

cmake --build . --config Release
if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo Build completed successfully!
echo Plugin DLL should be in: build\bin\Release\ButtonRemapperPlugin.dll
echo.
echo To use the plugin:
echo 1. Copy ButtonRemapperPlugin.dll to your UEVR plugins folder
echo 2. Launch UEVR and the game
echo 3. Configure button mappings in the "Button Remapper Configuration" window
echo 4. Save your configuration - it will be stored in button_remapper.json
echo.