@echo off
setlocal

echo ======================================================================
echo  HeidiSQL AntiFreeze Verification Test Suite
echo ======================================================================

if not exist "bin\test_harness.exe" (
    echo [*] Test harness binary not found in bin\. Building project first...
    call build.bat
    if errorlevel 1 (
        echo [ERROR] Build failed. Cannot run test suite.
        exit /b 1
    )
)

if not exist "bin\libmariadb.dll" (
    echo [ERROR] bin\libmariadb.dll not found.
    exit /b 1
)

echo [*] Running verification test harness...
echo.
bin\test_harness.exe bin\libmariadb.dll
set "TEST_EXIT=%ERRORLEVEL%"

echo.
if %TEST_EXIT% equ 0 (
    echo [SUCCESS] All verification tests passed successfully!
) else (
    echo [FAILURE] Test harness exited with code %TEST_EXIT%!
)

exit /b %TEST_EXIT%
