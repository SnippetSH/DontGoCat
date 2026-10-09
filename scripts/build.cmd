@echo off
rem Usage: scripts\build.cmd [build-dir=build] [target]
rem   e.g. scripts\build.cmd build-wp2 ccat_platform
setlocal
set "ROOT=%~dp0.."
set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build"
set "TARGET=%~2"

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsx86_amd64.bat" >nul 2>nul

if not exist "%ROOT%\%BUILD_DIR%\build.ninja" (
    cmake -S "%ROOT%" -B "%ROOT%\%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Debug || exit /b 1
)

if "%TARGET%"=="" (
    cmake --build "%ROOT%\%BUILD_DIR%"
) else (
    cmake --build "%ROOT%\%BUILD_DIR%" --target %TARGET%
)
exit /b %ERRORLEVEL%
