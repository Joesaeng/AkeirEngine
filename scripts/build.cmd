@echo off
rem Project_ME build (cmd version). Docs: Docs/BUILD.md  (ASCII only: cmd.exe mis-parses UTF-8 comments)
rem usage: scripts\build.cmd [preset] [configure|build|test|all] [target]
rem   preset defaults to msvc-debug, action defaults to all (configure+build)
setlocal
set PRESET=%~1
if "%PRESET%"=="" set PRESET=msvc-debug
set ACTION=%~2
if "%ACTION%"=="" set ACTION=all
set TARGET=%~3

set "INSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
set "PATH=%INSTALLER%;%SystemRoot%\System32;%SystemRoot%;%PATH%"
for /f "usebackq tokens=*" %%i in (`"%INSTALLER%\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if "%VSPATH%"=="" ( echo Visual Studio 2022 with C++ tools not found & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0.."

if "%ACTION%"=="configure" ( cmake --preset %PRESET% & exit /b %errorlevel% )
if "%ACTION%"=="build" (
  if "%TARGET%"=="" ( cmake --build --preset %PRESET% ) else ( cmake --build --preset %PRESET% --target %TARGET% )
  exit /b %errorlevel%
)
if "%ACTION%"=="test" ( ctest --preset %PRESET% & exit /b %errorlevel% )
cmake --preset %PRESET% || exit /b 1
if "%TARGET%"=="" ( cmake --build --preset %PRESET% ) else ( cmake --build --preset %PRESET% --target %TARGET% )
exit /b %errorlevel%
