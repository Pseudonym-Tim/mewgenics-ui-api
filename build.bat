@echo off
REM Build UIModTest.dll

set "MOD_NAME=UIModTest"
set "DESTINATION_DIR=C:\Users\Pseudonym_Tim\Desktop\Tools\Mewtator\mods\UIModTest"

REM true = deploy directly to an existing Mewtator mod folder
REM false = deploy to gameRoot\mods\UIModTest
set "MEWTATOR_DEPLOY=true"

setlocal

REM Locate Visual Studio via vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Is Visual Studio installed?
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
    set "VSDIR=%%i"
)

if not defined VSDIR (
    echo ERROR: Could not find a Visual Studio installation.
    pause
    exit /b 1
)

if not exist "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo ERROR: vcvarsall.bat not found at "%VSDIR%\VC\Auxiliary\Build\"
    pause
    exit /b 1
)

echo Setting up x64 MSVC environment...
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

echo.
echo Building %MOD_NAME%.dll...

cl /LD /O2 /GS- /W3 /D_CRT_SECURE_NO_WARNINGS /TC src\UIModTest.c src\mew_ui_api.c /Fe:%MOD_NAME%.dll /link user32.lib kernel32.lib

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build FAILED.
    pause
    exit /b 1
)

echo.
echo Build succeeded!

REM Clean intermediate files
del /Q UIModTest.obj mew_ui_api.obj %MOD_NAME%.lib %MOD_NAME%.exp 2>nul

REM Determine deploy path
if /I "%MEWTATOR_DEPLOY%"=="true" (
    set "DEPLOY_DIR=%DESTINATION_DIR%"
) else (
    set "DEPLOY_DIR=%DESTINATION_DIR%\mods\%MOD_NAME%"
)

REM Create deploy directory if needed
if not exist "%DEPLOY_DIR%" (
    mkdir "%DEPLOY_DIR%"
)

REM Deploy main DLL
copy /Y %MOD_NAME%.dll "%DEPLOY_DIR%\%MOD_NAME%.dll"

if not exist "%DEPLOY_DIR%\data\text" (
    mkdir "%DEPLOY_DIR%\data\text"
)

copy /Y "data\text\combined.csv.append" "%DEPLOY_DIR%\data\text\combined.csv.append"

REM Deploy swfs folder and its contents...
if exist "%~dp0swfs" (
    xcopy "%~dp0swfs" "%DEPLOY_DIR%\swfs" /E /I /Y
) else (
    echo WARNING: swfs folder not found!
)

echo.
echo Deployed to %DEPLOY_DIR%
pause