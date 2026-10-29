@echo off
rem build the 32-bit release and copy the output into build\release\bin
rem the build tree lives next to the repository because cmake presets and the
rem precompiled header break when the repository path contains non-ascii characters

setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
for /f "delims=" %%I in ('C:\Windows\System32\cmd.exe /c "for %%A in ("!ROOT!") do @echo %%~fA"') do set "ROOT=%%I"
for /f "delims=" %%I in ('C:\Windows\System32\cmd.exe /c "for %%A in ("!ROOT!") do @echo %%~sA"') do set "SOURCE=%%I"
for /f "delims=" %%I in ('C:\Windows\System32\cmd.exe /c "for %%A in ("!ROOT!\..") do @echo %%~fA"') do set "PARENT=%%I"
set "BUILD=%TEMP%\osustablemod-build\release"

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
goto build

:find_vs
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo visual studio not found
  exit /b 1
)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCDIR=%%I"
if not defined VCDIR (
  echo visual studio with the desktop c++ workload not found
  exit /b 1
)
set "VCVARS=!VCDIR!\VC\Auxiliary\Build\vcvarsall.bat"
exit /b 0

:build
call "!VCVARS!" x86 >nul
if errorlevel 1 exit /b 1

cmake -S "!SOURCE!" -B "!BUILD!" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON ^
  -DCMAKE_CXX_FLAGS="/MP /EHa /GS- /wd4100 /fp:fast /Oy" ^
  -DCMAKE_CXX_FLAGS_RELEASE="/O2 /Oi /Gy /GT /GF /GL" ^
  -DCMAKE_EXE_LINKER_FLAGS_RELEASE="/OPT:REF /OPT:ICF /LTCG" ^
  -DCMAKE_SHARED_LINKER_FLAGS_RELEASE="/OPT:REF /OPT:ICF /LTCG"
if errorlevel 1 exit /b 1

cmake --build "!BUILD!"
if errorlevel 1 exit /b 1

if not exist "!ROOT!\build\release\bin" mkdir "!ROOT!\build\release\bin"
copy /y "!BUILD!\bin\DiscordHook.dll" "!ROOT!\build\release\bin\" >nul
copy /y "!BUILD!\bin\DiscordSetup.exe" "!ROOT!\build\release\bin\" >nul

echo.
echo built into !ROOT!\build\release\bin
exit /b 0
