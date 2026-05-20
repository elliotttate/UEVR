@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "E:\Github\UEVRJ\build\uevr.vcxproj" /p:Configuration=Release /p:Platform=x64 /m /v:minimal /clp:Summary
exit /b %errorlevel%
