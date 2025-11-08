@echo off
echo Building EcosysQT project...

REM check conda
if defined CONDA_DEFAULT_ENV (
    echo Warning: CONDA ENV DETECTED: %CONDA_DEFAULT_ENV%
    echo TO AVOID POTENTIAL CONFLICTS, PLEASE DEACTIVATE THE ENVIRONMENT BEFORE RUNNING THIS SCRIPT
    pause
    exit /b 1
)


REM Check if VCPKG_ROOT is set
if not defined VCPKG_ROOT (
    echo Error: VCPKG_ROOT environment variable is not set
    echo Please run setup_vcpkg.bat or setup_vcpkg.ps1 first
    @REM pause
    exit /b 1
)

REM Check if vcpkg.exe exists
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo Error: vcpkg.exe not found in %VCPKG_ROOT%
    @REM pause
    exit /b 1
)

echo Using vcpkg path: %VCPKG_ROOT%

REM Create build directory
if not exist build mkdir build

REM Configure project
echo Configuring project...
cmake --preset default
IF ERRORLEVEL 1 (
    echo Configure failed!
    @REM pause
    exit /b 1
)

REM Build project
echo Building project...
cmake --build --preset default
IF ERRORLEVEL 1 (
    echo Build failed!
    @REM pause
    exit /b 1
)

echo.
echo Build succeeded!
echo Executable is at: build\Debug\MyQtApp.exe OR build-msvc\Debug\MyQtApp.exe OR build-nmake\MyQtApp.exe
echo.
@REM pause