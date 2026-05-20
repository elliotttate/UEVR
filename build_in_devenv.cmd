@echo off
rem Use vcvarsall.bat directly which is more reliable than VsDevCmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b 1
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "E:\Github\UEVRJ\build\uevr.vcxproj" /p:Configuration=Release /p:Platform=x64 /m /v:minimal /clp:Summary
