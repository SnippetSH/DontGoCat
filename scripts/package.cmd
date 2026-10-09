@echo off
rem Usage: scripts\package.cmd
rem   Release build -> dist\DontGoCat\ (DontGoCat.exe + Qt runtime + app-local MSVC runtime + license files)
rem   -> dist\DontGoCat-<version>-portable.zip -> (if Inno Setup is found) dist\DontGoCat-Setup-<version>.exe
rem   Single source of the version: project(DontGoCat VERSION ...) in CMakeLists.txt (build-release\ccat_version.txt).
rem   The Qt location is read from the CMake cache (WINDEPLOYQT_EXECUTABLE).
rem   Keep this file ASCII-only; Korean text lives in scripts\package_no_iscc.txt (UTF-8, printed with type).
setlocal EnableExtensions EnableDelayedExpansion
pushd "%~dp0.." || exit /b 1
set "ROOT=%CD%"
set "BUILD_DIR=%ROOT%\build-release"
set "DIST=%ROOT%\dist"
set "APP_DIR=%DIST%\DontGoCat"

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsx86_amd64.bat" >nul 2>nul

echo [1/5] Release build (build-release)
cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCCAT_BUILD_TESTS=OFF || goto :fail
cmake --build "%BUILD_DIR%" --target ccat || goto :fail

set "VERSION="
if exist "%BUILD_DIR%\ccat_version.txt" set /p VERSION=<"%BUILD_DIR%\ccat_version.txt"
if "%VERSION%"=="" (
    echo Could not read the version from %BUILD_DIR%\ccat_version.txt
    goto :fail
)
echo     version %VERSION%

set "WINDEPLOYQT="
for /f "tokens=2 delims==" %%i in ('findstr /b /c:"WINDEPLOYQT_EXECUTABLE:" "%BUILD_DIR%\CMakeCache.txt"') do set "WINDEPLOYQT=%%i"
if "%WINDEPLOYQT%"=="" (
    echo Could not find windeployqt in the CMake cache.
    goto :fail
)

echo [2/5] Assemble dist\DontGoCat (windeployqt + MSVC runtime + licenses)
if exist "%APP_DIR%" rmdir /s /q "%APP_DIR%"
if exist "%APP_DIR%" (
    echo Cannot remove dist\DontGoCat. Quit any running DontGoCat.exe from that folder first.
    goto :fail
)
mkdir "%APP_DIR%" || goto :fail
copy /y "%BUILD_DIR%\DontGoCat.exe" "%APP_DIR%\" >nul || goto :fail

rem The app uses only Core/Gui/Widgets + platforms/qwindows + styles. Images come from the built-in PNG
rem handler (Qt resources), so imageformats / iconengines (svg) / generic / networkinformation / tls (network)
rem plugins are not needed.
"%WINDEPLOYQT%" --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --no-ffmpeg ^
    --no-compiler-runtime --skip-plugin-types imageformats,iconengines,generic,networkinformation,tls,platforminputcontexts ^
    "%APP_DIR%\DontGoCat.exe" || goto :fail

rem MSVC runtime, app-local (VC redistributable DLLs may ship next to the app)
set "CRT_DIR="
for /d %%d in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do set "CRT_DIR=%%d"
if "%CRT_DIR%"=="" (
    echo Could not find the MSVC redistributable folder: !VCToolsRedistDir!x64
    goto :fail
)
for %%f in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll msvcp140_1.dll msvcp140_2.dll) do (
    if exist "%CRT_DIR%\%%f" copy /y "%CRT_DIR%\%%f" "%APP_DIR%\" >nul
)

rem License files (app license, third-party notices, Qt LGPL/GPL texts) ship in both the zip and the installer
copy /y "%ROOT%\LICENSE" "%APP_DIR%\LICENSE.txt" >nul || goto :fail
copy /y "%ROOT%\THIRD_PARTY_NOTICES.txt" "%APP_DIR%\THIRD_PARTY_NOTICES.txt" >nul || goto :fail
mkdir "%APP_DIR%\licenses" || goto :fail
copy /y "%ROOT%\licenses\Qt-LGPL-3.0-GPL-3.0.txt" "%APP_DIR%\licenses\Qt-LGPL-3.0-GPL-3.0.txt" >nul || goto :fail

echo [3/5] Portable zip
set "ZIP=%DIST%\DontGoCat-%VERSION%-portable.zip"
if exist "%ZIP%" del /q "%ZIP%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%APP_DIR%' -DestinationPath '%ZIP%' -CompressionLevel Optimal" || goto :fail
echo     %ZIP%

echo [4/5] Installer (Inno Setup 6)
set "ISCC="
for %%p in ("%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe" "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe") do (
    if not defined ISCC if exist %%p set "ISCC=%%~p"
)
if not defined ISCC (
    for /f "delims=" %%p in ('where ISCC.exe 2^>nul') do if not defined ISCC set "ISCC=%%p"
)
if not defined ISCC goto :no_iscc
"%ISCC%" /Qp "/DAppVersion=%VERSION%" "%ROOT%\installer\ccat.iss" || goto :fail
echo [5/5] Done
echo     %ZIP%
echo     %DIST%\DontGoCat-Setup-%VERSION%.exe
popd
exit /b 0

:no_iscc
echo     Inno Setup 6 (ISCC.exe) not found; only the portable zip was created.
rem Korean notice (UTF-8 text file; switch the console code page just for this output)
set "OLD_CP="
for /f "tokens=2 delims=:" %%c in ('chcp') do set "OLD_CP=%%c"
set "OLD_CP=%OLD_CP: =%"
chcp 65001 >nul
type "%~dp0package_no_iscc.txt"
if defined OLD_CP chcp %OLD_CP% >nul
echo [5/5] Done: portable zip only
popd
exit /b 0

:fail
echo Packaging failed.
popd
exit /b 1
