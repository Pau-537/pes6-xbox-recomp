@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
rem Auto-detect the MinGW that Qt's installer put in C:\Qt\Tools (or use one already on PATH)
for /d %%D in (C:\Qt\Tools\mingw*_64) do set "MINGW=%%D"
if defined MINGW set "PATH=%MINGW%\bin;%PATH%"
where g++ >nul 2>nul || (echo [ERROR] g++ not found. Install MinGW via Qt Maintenance Tool. & goto :end)
where cmake >nul 2>nul || (echo [ERROR] cmake not found. & goto :end)

cmake -S . -B build_mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release || (echo [ERROR] Configure failed. & goto :end)
cmake --build build_mingw || (echo [ERROR] Build failed. & goto :end)

if exist dist rmdir /s /q dist
mkdir dist
copy /y build_mingw\installer-gui.exe dist\ >nul
copy /y build_mingw\installer-cli.exe dist\ >nul
copy /y manifest.txt dist\ >nul
xcopy /e /i /y payload dist\payload >nul
echo.
echo === SUCCESS ===  Your installer is in the "dist" folder.
:end
echo.
pause
