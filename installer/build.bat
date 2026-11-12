@echo off
setlocal
cd /d "%~dp0"

set "ISCC="

if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\iscc.exe" (
    set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\iscc.exe"
) else if exist "%ProgramFiles(x86)%\Inno Setup 6\iscc.exe" (
    set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\iscc.exe"
) else if exist "%ProgramFiles%\Inno Setup 6\iscc.exe" (
    set "ISCC=%ProgramFiles%\Inno Setup 6\iscc.exe"
) else (
    where iscc.exe >nul 2>nul
    if %errorlevel% equ 0 (
        set "ISCC=iscc.exe"
    )
)

if "%ISCC%"=="" (
    echo [ERROR] Inno Setup 6 iscc.exe was not found.
    echo Please install Inno Setup 6 or run: winget install JRSoftware.InnoSetup
    exit /b 1
)

echo Compiling installer with: "%ISCC%"
"%ISCC%" miniosd.iss
if %errorlevel% neq 0 (
    echo [ERROR] Failed to compile installer.
    exit /b %errorlevel%
)

echo.
echo [SUCCESS] MiniOSD-Setup.exe built successfully!
