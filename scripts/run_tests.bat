@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT_DIR=%%~fI"
set "BIN_DIR=%ROOT_DIR%\bin"

if not exist "%BIN_DIR%\test_runner.exe" (
    echo test_runner.exe not found in %BIN_DIR%. Running build.bat first...
    call "%SCRIPT_DIR%build.bat"
    if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
)

"%BIN_DIR%\test_runner.exe"
exit /b %ERRORLEVEL%
