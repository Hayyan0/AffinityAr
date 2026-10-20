@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo Building Affinity Arabic Installer (Tauri 2)
echo ========================================================

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT_DIR=%%~fI"
set "INSTALLER_DIR=%ROOT_DIR%\installer\src-tauri"
set "BIN_DIR=%ROOT_DIR%\bin"

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

:: Ensure all required runtime binaries are built before compiling installer
if not exist "%BIN_DIR%\AffinityArabic.exe" (
    echo Required binary AffinityArabic.exe not found in %BIN_DIR%.
    echo Running build.bat first...
    call "%SCRIPT_DIR%build.bat"
    if !ERRORLEVEL! neq 0 (
        echo Failed to build required runtime binaries!
        exit /b !ERRORLEVEL!
    )
)
if not exist "%BIN_DIR%\affinity_arabic.dll" (
    echo Required binary affinity_arabic.dll not found in %BIN_DIR%.
    echo Running build.bat first...
    call "%SCRIPT_DIR%build.bat"
    if !ERRORLEVEL! neq 0 (
        echo Failed to build required runtime binaries!
        exit /b !ERRORLEVEL!
    )
)
if not exist "%BIN_DIR%\libstacktrace.dll" (
    echo Required binary libstacktrace.dll not found in %BIN_DIR%.
    echo Running build.bat first...
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

copy /y "target\release\affinity-arabic-installer.exe" "%BIN_DIR%\AffinityArabicSetup.exe" >nul
copy /y "target\release\affinity-arabic-installer.exe" "%BIN_DIR%\affinity-arabic-installer.exe" >nul
if %ERRORLEVEL% neq 0 (
    echo Failed to copy installer binary into bin folder!
    popd
    exit /b 1
)

popd

echo.
echo ========================================================
echo Installer Build Successful!
echo Installer: %BIN_DIR%\AffinityArabicSetup.exe
echo ========================================================
exit /b 0
