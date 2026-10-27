@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo Building Affinity Arabic Installer (Tauri 2)
echo ========================================================

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT_DIR=%%~fI"
set "INSTALLER_DIR=%ROOT_DIR%\installer\src-tauri"
set "BIN_DIR=%ROOT_DIR%\bin"

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%verify_version.ps1"
if %ERRORLEVEL% neq 0 (
    echo Error: version metadata is inconsistent.
    exit /b 1
)

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

if "%SKIP_NATIVE_BUILD%"=="1" (
    if not exist "%BIN_DIR%\AffinityArabic.exe" goto :missing_payload
    if not exist "%BIN_DIR%\affinity_arabic.dll" goto :missing_payload
    if not exist "%BIN_DIR%\libstacktrace.dll" goto :missing_payload
) else (
    echo Rebuilding native payloads...
    call "%SCRIPT_DIR%build.bat"
    if !ERRORLEVEL! neq 0 (
        echo Failed to build required runtime binaries!
        exit /b !ERRORLEVEL!
    )
)

pushd "%INSTALLER_DIR%"

cargo build --release
if %ERRORLEVEL% neq 0 (
    echo Build failed for installer!
    popd
    exit /b 1
)

set "INSTALLER_SOURCE=target\release\affinity-arabic-installer.exe"
set "INSTALLER_NEW=%BIN_DIR%\AffinityArabicSetup.new.exe"
set "INSTALLER_OLD=%BIN_DIR%\AffinityArabicSetup.%RANDOM%.old.exe"
set "INSTALLER_DEST=%BIN_DIR%\AffinityArabicSetup.exe"

copy /y "%INSTALLER_SOURCE%" "%INSTALLER_NEW%" >nul
if %ERRORLEVEL% neq 0 (
    echo Failed to copy installer binary into %BIN_DIR%.
    popd
    exit /b 1
)

if exist "%INSTALLER_DEST%" (
    move /y "%INSTALLER_DEST%" "%INSTALLER_OLD%" >nul
    if !ERRORLEVEL! neq 0 (
        echo Failed to replace the existing AffinityArabicSetup.exe. Close it and retry.
        del /q "%INSTALLER_NEW%" >nul 2>&1
        popd
        exit /b 1
    )
)

move /y "%INSTALLER_NEW%" "%INSTALLER_DEST%" >nul
if %ERRORLEVEL% neq 0 (
    echo Failed to install the built installer into %BIN_DIR%.
    popd
    exit /b 1
)

del /q "%INSTALLER_OLD%" >nul 2>&1

if exist "%BIN_DIR%\affinity-arabic-installer.exe" del /q "%BIN_DIR%\affinity-arabic-installer.exe"

popd

echo.
echo ========================================================
echo Installer Build Successful!
echo Installer: %BIN_DIR%\AffinityArabicSetup.exe
echo ========================================================
exit /b 0

:missing_payload
echo Required native payload is missing from %BIN_DIR%.
echo Run scripts\build.bat or omit SKIP_NATIVE_BUILD.
exit /b 1
