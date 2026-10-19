@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo Building Affinity Arabic Hook and Standalone Test Runner
echo ========================================================

:: Determine project root directory relative to this script
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT_DIR=%%~fI"
set "SRC_DIR=%ROOT_DIR%\src"
set "BIN_DIR=%ROOT_DIR%\bin"
set "RES_DIR=%ROOT_DIR%\resources"
set "TESTS_DIR=%ROOT_DIR%\tests"

:: 1. Check if complete MSVC environment (cl.exe, rc.exe, and INCLUDE) is already set up
if defined INCLUDE (
    where cl.exe >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        where rc.exe >nul 2>&1
        if !ERRORLEVEL! equ 0 (
            echo Complete MSVC environment already detected in PATH.
            goto :msvc_ready
        )
    )
)

:: 2. Try vswhere.exe (official MSVC installer query)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
            goto :setup_vcvars
        )
    )
)

:: 3. Check standard Visual Studio installation directories
for %%V in (18 17 2022 2019) do (
    for %%E in (Community Professional Enterprise BuildTools) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
            goto :setup_vcvars
        )
        if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
            goto :setup_vcvars
        )
    )
)

echo Error: vcvars64.bat not found. Please install MSVC or run from a Developer Command Prompt.
exit /b 1

:setup_vcvars
echo Initializing MSVC from: %VCVARS%
call "%VCVARS%" >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error initializing MSVC environment.
    exit /b 1
)

:msvc_ready
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

pushd "%BIN_DIR%"

del /q *.old 2>nul
if exist "affinity_arabic.dll" move /y "affinity_arabic.dll" "affinity_arabic.dll.old" >nul 2>&1
if exist "version.dll" move /y "version.dll" "version.dll.old" >nul 2>&1
if exist "AffinityArabic.exe" move /y "AffinityArabic.exe" "AffinityArabic.exe.old" >nul 2>&1

echo Compiling resources...
rc.exe /nologo /i"%RES_DIR%" /fo"version_info.res" "%RES_DIR%\version.rc"
rc.exe /nologo /i"%RES_DIR%" /fo"launcher.res" "%RES_DIR%\launcher.rc"

echo Compiling affinity_arabic.dll (Hook DLL)...
cl.exe /nologo /O2 /MD /std:c++17 /EHsc /utf-8 ^
    /I"%SRC_DIR%" ^
    /LD "%SRC_DIR%\shaping\arabic_shaper.cpp" ^
        "%SRC_DIR%\shaping\dwrite_shaper.cpp" ^
        "%SRC_DIR%\bidi\bidi_engine.cpp" ^
        "%SRC_DIR%\core\hde\hde64.c" ^
        "%SRC_DIR%\core\pattern_scanner.cpp" ^
        "%SRC_DIR%\core\hook_engine.cpp" ^
        "%SRC_DIR%\core\credit_manager.cpp" ^
        "%SRC_DIR%\core\affinity_arabic_hook.cpp" ^
        "%SRC_DIR%\core\dllmain.cpp" ^
        version_info.res ^
    /link /OUT:"affinity_arabic.dll" /IMPLIB:"affinity_arabic.lib" user32.lib kernel32.lib dwrite.lib comctl32.lib shell32.lib

if %ERRORLEVEL% neq 0 (
    echo Build failed for affinity_arabic.dll!
    popd
    exit /b 1
)

echo.
echo Compiling test_runner.exe (Native C++ Test Suite)...
cl.exe /nologo /O2 /MD /std:c++17 /EHsc /utf-8 ^
    /I"%SRC_DIR%" ^
    "%SRC_DIR%\shaping\arabic_shaper.cpp" ^
    "%SRC_DIR%\shaping\dwrite_shaper.cpp" ^
    "%SRC_DIR%\bidi\bidi_engine.cpp" ^
    "%SRC_DIR%\core\hde\hde64.c" ^
    "%SRC_DIR%\core\pattern_scanner.cpp" ^
    "%SRC_DIR%\core\credit_manager.cpp" ^
    "%TESTS_DIR%\native_tests.cpp" ^
    /link /OUT:"test_runner.exe" kernel32.lib user32.lib dwrite.lib comctl32.lib shell32.lib

if %ERRORLEVEL% neq 0 (
    echo Build failed for test_runner.exe!
    popd
    exit /b 1
)

echo.
echo Compiling AffinityArabic.exe (1-Click Launcher with Custom Icon)...
cl.exe /nologo /O2 /MD /std:c++17 /EHsc /utf-8 ^
    /I"%SRC_DIR%" ^
    "%SRC_DIR%\launcher\launcher.cpp" ^
    launcher.res ^
    /link /OUT:"AffinityArabic.exe" /SUBSYSTEM:WINDOWS user32.lib kernel32.lib shell32.lib comctl32.lib

echo.
echo Compiling version.dll (Seamless Proxy DLL)...
cl.exe /nologo /O2 /MD /std:c++17 /EHsc /utf-8 ^
    /I"%SRC_DIR%" ^
    /LD "%SRC_DIR%\proxy\version_proxy.cpp" ^
        "%SRC_DIR%\shaping\arabic_shaper.cpp" ^
        "%SRC_DIR%\shaping\dwrite_shaper.cpp" ^
        "%SRC_DIR%\bidi\bidi_engine.cpp" ^
        "%SRC_DIR%\core\hde\hde64.c" ^
        "%SRC_DIR%\core\pattern_scanner.cpp" ^
        "%SRC_DIR%\core\hook_engine.cpp" ^
        "%SRC_DIR%\core\credit_manager.cpp" ^
        "%SRC_DIR%\core\affinity_arabic_hook.cpp" ^
        version_info.res ^
    /link /OUT:"version.dll" /DEF:"%SRC_DIR%\proxy\version.def" user32.lib kernel32.lib dwrite.lib comctl32.lib shell32.lib

echo.
echo Compiling libstacktrace.dll (Seamless App Proxy DLL)...
cl.exe /nologo /O2 /MD /std:c++17 /EHsc /utf-8 ^
    /I"%SRC_DIR%" ^
    /LD "%SRC_DIR%\proxy\libstacktrace_proxy.cpp" ^
        "%SRC_DIR%\shaping\arabic_shaper.cpp" ^
        "%SRC_DIR%\shaping\dwrite_shaper.cpp" ^
        "%SRC_DIR%\bidi\bidi_engine.cpp" ^
        "%SRC_DIR%\core\hde\hde64.c" ^
        "%SRC_DIR%\core\pattern_scanner.cpp" ^
        "%SRC_DIR%\core\hook_engine.cpp" ^
        "%SRC_DIR%\core\credit_manager.cpp" ^
        "%SRC_DIR%\core\affinity_arabic_hook.cpp" ^
        version_info.res ^
    /link /OUT:"libstacktrace.dll" /DEF:"%SRC_DIR%\proxy\libstacktrace.def" user32.lib kernel32.lib dwrite.lib comctl32.lib shell32.lib

if %ERRORLEVEL% neq 0 (
    echo Build failed for libstacktrace.dll!
    popd
    exit /b 1
)

del /q *.obj *.exp *.lib *.old *.res 2>nul
popd

echo.
echo ========================================================
echo Build Successful!
echo Hook DLL: %BIN_DIR%\affinity_arabic.dll
echo Proxy DLL: %BIN_DIR%\version.dll
echo Seamless DLL: %BIN_DIR%\libstacktrace.dll
echo Test Runner: %BIN_DIR%\test_runner.exe
echo Launcher: %BIN_DIR%\AffinityArabic.exe
echo ========================================================
exit /b 0
