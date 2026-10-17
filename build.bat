@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo  Building HeidiSQL MariaDB Proxy DLL and Verification Test Harness
echo ======================================================================

if exist "%USERPROFILE%\Documents\qwen3.8\runtimes\vs-buildtools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=%USERPROFILE%\Documents\qwen3.8\runtimes\vs-buildtools\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
) else (
    set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

if not exist "!VCVARS!" (
    echo [ERROR] Visual C++ build environment not found at:
    echo "!VCVARS!"
    exit /b 1
)

echo [*] Initializing MSVC x64 build environment...
call "!VCVARS!" >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to initialize vcvars64.
    exit /b 1
)

if not exist "bin" mkdir "bin"

echo [*] Compiling proxy DLL: bin\libmariadb.dll...
cl.exe /nologo /O2 /MD /W4 /EHsc /std:c++17 /Isrc ^
    src\proxy_main.cpp src\async_engine.cpp src\ui_overlay.cpp ^
    /Fe:bin\libmariadb.dll /LD ^
    /link /DEF:src\exports.def user32.lib gdi32.lib comctl32.lib

if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to compile libmariadb.dll
    exit /b 1
)

echo [*] Compiling test harness: bin\test_harness.exe...
cl.exe /nologo /O2 /MD /W4 /EHsc /std:c++17 /Isrc ^
    src\test_harness.cpp ^
    /Fe:bin\test_harness.exe user32.lib

if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to compile test_harness.exe
    exit /b 1
)

del /q bin\*.obj bin\*.exp >nul 2>&1

echo.
echo ======================================================================
echo  BUILD COMPLETE!
echo  Artifacts generated:
echo    - bin\libmariadb.dll
echo    - bin\libmariadb.lib
echo    - bin\test_harness.exe
echo ======================================================================
