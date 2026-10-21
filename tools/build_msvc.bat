@echo off
rem Configure + build with MSVC and NMake. Usage: tools\build_msvc.bat <build-dir> [frontend ON/OFF] [SDL2 dir]
setlocal
set "BUILD=%~1"
if "%BUILD%"=="" set "BUILD=build-msvc"
set "FRONTEND=%~2"
if "%FRONTEND%"=="" set "FRONTEND=ON"
set "SDLDIR=%~3"
call "%~dp0msvc_env.bat"
if not exist "%BUILD%\CMakeCache.txt" (
  cmake -S . -B "%BUILD%" -G "NMake Makefiles" "-DCMAKE_BUILD_TYPE=Release" "-DQE_FRONTEND=%FRONTEND%" "-DSDL2_DIR=%SDLDIR%" || exit /b 1
)
cmake --build "%BUILD%" || exit /b 1
