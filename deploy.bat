@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo  Deploying HeidiSQL Anti-Freeze MariaDB Proxy DLL
echo ======================================================================

if exist "%LOCALAPPDATA%\Programs\HeidiSQL" (
    set "TARGET_DIR=%LOCALAPPDATA%\Programs\HeidiSQL"
) else (
    set "TARGET_DIR=%ProgramFiles%\HeidiSQL"
)
set "PROXY_DLL=bin\libmariadb.dll"

if not exist "%PROXY_DLL%" (
    echo [ERROR] Compiled proxy DLL not found at: %PROXY_DLL%
    echo Please run build.bat first!
    exit /b 1
)

if not exist "%TARGET_DIR%" (
    echo [ERROR] Target HeidiSQL directory does not exist: %TARGET_DIR%
    exit /b 1
)

echo [*] Checking if HeidiSQL is currently running...
tasklist /FI "IMAGENAME eq heidisql.exe" 2>NUL | find /I /N "heidisql.exe">NUL
if "%ERRORLEVEL%"=="0" (
    echo [WARNING] heidisql.exe is currently running!
    echo Please close HeidiSQL before deploying to avoid file locking.
)

pushd "%TARGET_DIR%"

if not exist "libmariadb_orig.dll" (
    if exist "libmariadb.dll" (
        echo [*] Creating primary original backup: libmariadb_orig.dll...
        copy /y "libmariadb.dll" "libmariadb_orig.dll" >nul
    )
)

if not exist "libmariadb.dll.bak" (
    if exist "libmariadb.dll" (
        echo [*] Creating safeguard backup: libmariadb.dll.bak...
        copy /y "libmariadb.dll" "libmariadb.dll.bak" >nul
    )
)

popd

echo [*] Copying compiled proxy DLL into HeidiSQL directory...
copy /y "%PROXY_DLL%" "%TARGET_DIR%\libmariadb.dll" >nul

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] Proxy DLL deployed successfully!
    echo Target:   %TARGET_DIR%\libmariadb.dll
    echo Original: %TARGET_DIR%\libmariadb_orig.dll
    echo Backup:   %TARGET_DIR%\libmariadb.dll.bak
) else (
    echo [ERROR] Failed to copy proxy DLL into %TARGET_DIR%.
    exit /b 1
)
