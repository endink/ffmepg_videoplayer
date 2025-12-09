@echo off
setlocal enabledelayedexpansion

:: ----------------------------------------
:: Configuration paths
:: ----------------------------------------
set "UNREAL_PLUGIN_DIR=F:\3D_Works\UE\MediaPipe4U\MediaPipe4U\Source\ThirdParty\videoplayer"

pushd %~dp0
set "SCRIPT_DIR=%CD%"
set "INSTALL_DIR=%SCRIPT_DIR%\install"

echo.
echo ===== Video Player Deploy Script =====
echo.

:: ----------------------------------------
:: Check if UNREAL_PLUGIN_DIR exists
:: ----------------------------------------
if not exist "%UNREAL_PLUGIN_DIR%" (
    echo [ERROR] UNREAL_PLUGIN_DIR does not exist:
    echo   %UNREAL_PLUGIN_DIR%
    echo Please verify the path.

    pause
    exit /b 1
)

echo [OK] Plugin directory found:
echo   %UNREAL_PLUGIN_DIR%
echo.

:: ----------------------------------------
:: Clear all content inside UNREAL_PLUGIN_DIR
:: ----------------------------------------
echo Cleaning plugin directory...


:: Delete all subdirectories
for /d %%i in ("%UNREAL_PLUGIN_DIR%\*") do (
    rd /s /q "%%i"
)

echo [OK] Directory cleaned.
echo.

:: ----------------------------------------
:: Copy content from INSTALL_DIR to UNREAL_PLUGIN_DIR
:: ----------------------------------------
if not exist "%INSTALL_DIR%" (
    echo [ERROR] INSTALL_DIR does not exist:
    echo   %INSTALL_DIR%
    echo Cannot continue copying.
    pause
    exit /b 1
)

echo Copying new files to plugin directory...
xcopy "%INSTALL_DIR%\*" "%UNREAL_PLUGIN_DIR%\" /e /i /y >nul

if %errorlevel% neq 0 (
    echo [ERROR] File copy failed.
    pause
    exit /b 1
)

echo [OK] Files copied successfully!
echo.
echo video player libs deploy succeed !
echo.

pause
