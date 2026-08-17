@echo off
REM Builds the plugin.webm Windows plugin (plugin_geometry2d.dll, Win32/x86) and packages
REM it into plugins\2025.3720\win32-sim. Run build_thrid_party_libs.ps1 first, and
REM make sure vpx.lib / webm.lib / opus.lib (+ OpenAL32.lib) are reachable by the
REM linker. UNVERIFIED on the authoring machine (macOS) — build on Windows.
REM
REM Run from a Visual Studio "Developer Command Prompt" (so msbuild is on PATH).
setlocal
set HERE=%~dp0
set CONFIG=Release

if not exist "%HERE%third_party_libs\vpx.lib" (
  echo ERROR: third_party_libs\vpx.lib missing. Run build_thrid_party_libs.ps1 first. 1>&2
  exit /b 1
)

msbuild "%HERE%Plugin.sln" /p:Configuration=%CONFIG% /p:Platform=Win32 /m
if errorlevel 1 exit /b 1

REM Locate the built DLL (sln default puts Win32 output in <dir>\Release).
set DLL=%HERE%%CONFIG%\plugin_geometry2d.dll
if not exist "%DLL%" set DLL=%HERE%Win32\%CONFIG%\plugin_geometry2d.dll
if not exist "%DLL%" (
  echo ERROR: plugin_geometry2d.dll not found after build. 1>&2
  exit /b 1
)

set DST=%HERE%..\..\plugins\2025.3720\win32-sim
if not exist "%DST%" mkdir "%DST%"
copy /Y "%DLL%" "%DST%\plugin_geometry2d.dll"

echo Packing win32-sim...
pushd "%DST%"
tar -czf "%HERE%2025.3720-win32-sim.tgz" plugin_geometry2d.dll
popd
echo   -^> %DST%\plugin_geometry2d.dll
echo   -^> %HERE%2025.3720-win32-sim.tgz
echo Done.
