@echo off
echo Building FPSCounterPlugin...

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
echo Plugin DLL should be in: build\bin\Release\FPSCounterPlugin.dll
echo.