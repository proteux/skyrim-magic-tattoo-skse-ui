@echo off
rem Runs the given command inside an x64 MSVC developer environment.
setlocal
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo No Visual Studio install with the C++ toolset was found. 1>&2
  exit /b 1
)
set "KEEP_VCPKG_ROOT=%VCPKG_ROOT%"
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if defined KEEP_VCPKG_ROOT set "VCPKG_ROOT=%KEEP_VCPKG_ROOT%"
%*
