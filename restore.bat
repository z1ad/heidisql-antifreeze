@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo  Restoring Original HeidiSQL libmariadb.dll
echo ======================================================================

if exist "%LOCALAPPDATA%\Programs\HeidiSQL" (
    set "TARGET_DIR=%LOCALAPPDATA%\Programs\HeidiSQL"
) else (
    set "TARGET_DIR=%ProgramFiles%\HeidiSQL"
)

if not exist "%TARGET_DIR%" (
    echo [ERROR] HeidiSQL directory not found at: %TARGET_DIR%
    exit /b 1
)

pushd "%TARGET_DIR%"

if exist "libmariadb_orig.dll" (
    echo [*] Restoring from libmariadb_orig.dll...
    copy /y "libmariadb_orig.dll" "libmariadb.dll" >nul
    echo [SUCCESS] Restored original libmariadb.dll from libmariadb_orig.dll.
) else if exist "libmariadb.dll.bak" (
    echo [*] Restoring from libmariadb.dll.bak...
    copy /y "libmariadb.dll.bak" "libmariadb.dll" >nul
    echo [SUCCESS] Restored original libmariadb.dll from libmariadb.dll.bak.
) else (
    echo [ERROR] No backup found (neither libmariadb_orig.dll nor libmariadb.dll.bak exist).
)

popd
