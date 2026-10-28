@echo off
setlocal
cd /d "%~dp0"
where cmake >nul 2>nul || (echo [ERROR] cmake not found. Install CMake from cmake.org and tick "Add to PATH". & goto :end)

echo === Configuring (Visual Studio) ===
cmake -S . -B build -A x64 || (echo [ERROR] Configure failed. Is "Desktop development with C++" installed in Visual Studio? & goto :end)

echo === Building ===
cmake --build build --config Release || (echo [ERROR] Build failed. & goto :end)

if exist dist rmdir /s /q dist
mkdir dist
copy /y build\Release\installer-gui.exe dist\ >nul
copy /y build\Release\installer-cli.exe dist\ >nul
copy /y manifest.txt dist\ >nul
xcopy /e /i /y payload dist\payload >nul
echo.
echo === SUCCESS ===  Your installer is in the "dist" folder.
:end
echo.
pause
