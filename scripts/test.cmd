@echo off
rem Usage: scripts\test.cmd [build-dir=build] [extra ctest args]
rem   e.g. scripts\test.cmd build -R test_brain
rem Build first with scriptsbuild.cmd.
setlocal
set "ROOT=%~dp0.."
set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build"
shift

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsx86_amd64.bat" >nul 2>nul

set "ARGS="
:collect
if "%~1"=="" goto run
set ARGS=%ARGS% %1
shift
goto collect

:run
ctest --test-dir "%ROOT%\%BUILD_DIR%" --output-on-failure %ARGS%
exit /b %ERRORLEVEL%
