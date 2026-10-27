@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT_DIR=%%~fI"
set "BIN_DIR=%ROOT_DIR%\bin"

set "SKIP_TESTS="
call "%SCRIPT_DIR%build.bat"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

if not exist "%BIN_DIR%\test_runner.exe" (
    echo test_runner.exe was not built. Is tests\native_tests.cpp present?
    exit /b 1
)

"%BIN_DIR%\test_runner.exe"
exit /b %ERRORLEVEL%
