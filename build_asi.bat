@echo off
REM One-click build of weaponloader.asi (32-bit). Double-click this file.
REM Works with EITHER: Visual Studio / Build Tools (x86) OR MinGW-w64 i686 (e.g. w64devkit) on PATH.
setlocal
cd /d "%~dp0"
if exist weaponloader.asi del weaponloader.asi

REM --- try MinGW i686 first
where i686-w64-mingw32-g++ >nul 2>nul
if %errorlevel%==0 (
  echo Building with MinGW i686...
  i686-w64-mingw32-g++ -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ -Wl,--kill-at -o weaponloader.asi plugin\plugin.cpp -lwinmm
  goto done
)
REM --- then Visual Studio: find vcvars32 via vswhere
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSW%" (
  for /f "usebackq delims=" %%i in (`"%VSW%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR (
  echo Building with Visual Studio at %VSDIR% ...
  call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul
  cl /nologo /LD /MT /EHsc /O2 /std:c++17 plugin\plugin.cpp /Fe:weaponloader.asi /link winmm.lib
  goto done
)
echo.
echo No compiler found. Install ONE of these, then run this file again:
echo   * Visual Studio Build Tools (workload "Desktop development with C++"), or
echo   * w64devkit for 32-bit / MinGW-w64 i686 added to PATH.
echo Or use the GitHub Actions workflow in .github\workflows (builds in the cloud, no install needed).
exit /b 1

:done
if exist weaponloader.asi (
  echo.
  echo SUCCESS: weaponloader.asi created in %cd%
  echo Next: follow INSTALL_GUIDE.md section 3 (copy the .asi and the weaponloader folder next to gta3.exe).
) else (
  echo.
  echo BUILD FAILED. Copy the error text above and send it to me.
  exit /b 1
)
