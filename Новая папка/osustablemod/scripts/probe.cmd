@echo off
setlocal enabledelayedexpansion
set "ROOT=%~dp0.."
for /f "delims=" %%I in ('C:\Windows\System32\cmd.exe /c "for %%A in ("!ROOT!") do @echo %%~fA"') do set "ROOT=%%I"
for /f "delims=" %%I in ('C:\Windows\System32\cmd.exe /c "for %%A in ("!ROOT!\..") do @echo %%~fA"') do set "PARENT=%%I"
set "BUILD=!PARENT!\osustablemod-build\release"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
cmake --build "!BUILD!"
echo EXIT=%errorlevel%
