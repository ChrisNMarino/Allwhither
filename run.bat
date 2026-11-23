@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "PLATFORM=%~1"
set "PROJECT=%~2"

if /I "%PLATFORM%"=="--help" goto usage
if /I "%PLATFORM%"=="-h" goto usage
if not "%~3"=="" goto invalid_usage

if not defined PLATFORM set /p "PLATFORM=Platform (linux/windows/android/web): "
if not defined PROJECT call :prompt_project

if /I not "%PLATFORM%"=="linux" if /I not "%PLATFORM%"=="windows" if /I not "%PLATFORM%"=="android" if /I not "%PLATFORM%"=="web" goto invalid_platform
call :validate_project
if errorlevel 1 goto invalid_project

pushd "%ROOT_DIR%" || exit /b 1

if /I "%PLATFORM%"=="linux" goto run_linux
if /I "%PLATFORM%"=="windows" goto run_windows
if /I "%PLATFORM%"=="android" goto run_android
goto run_web

:run_linux
set "APP=build\%PROJECT%\linux\%PROJECT%"
if not exist "%APP%" goto missing_linux
"%APP%"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%

:run_windows
set "WIN_BASE=build\%PROJECT%\windows\bin\Omni.Windows"
if exist "%WIN_BASE%\debug\%PROJECT%.exe" (
    "%WIN_BASE%\debug\%PROJECT%.exe"
    goto finished
)
if exist "%WIN_BASE%\release\%PROJECT%.exe" (
    "%WIN_BASE%\release\%PROJECT%.exe"
    goto finished
)
if exist "%WIN_BASE%\debug\%PROJECT%.dll" (
    dotnet "%WIN_BASE%\debug\%PROJECT%.dll"
    goto finished
)
if exist "%WIN_BASE%\release\%PROJECT%.dll" (
    dotnet "%WIN_BASE%\release\%PROJECT%.dll"
    goto finished
)
echo No windows build for "%PROJECT%" at build\%PROJECT%\windows. 1>&2
echo Run build.bat windows %PROJECT% first. 1>&2
goto failed_status

:run_android
set "APK=build\%PROJECT%\android\app\outputs\apk\debug\%PROJECT%-debug.apk"
if not exist "%APK%" (
    echo No android build for "%PROJECT%" at %APK%. 1>&2
    echo Run build.bat android %PROJECT% first. 1>&2
    goto failed_status
)
call :find_adb
if errorlevel 1 goto failed_status
"%ADB%" devices | findstr /r /c:"	device$" >nul
if errorlevel 1 (
    echo No Android device or emulator is connected. 1>&2
    goto failed_status
)
"%ADB%" install -r "%APK%"
if errorlevel 1 goto failed
"%ADB%" shell am force-stop org.omni.starter
"%ADB%" shell am start -n org.omni.starter/.MainActivity
goto finished

:run_web
set "WEB_DIR=build\%PROJECT%\web"
if not exist "%WEB_DIR%\payload.json" (
    echo No web build for "%PROJECT%" at %WEB_DIR%. 1>&2
    echo Run build.bat web %PROJECT% first. 1>&2
    goto failed_status
)
if defined PORT (set "WEB_PORT=%PORT%") else set "WEB_PORT=8080"
echo Serving http://127.0.0.1:%WEB_PORT%/
node platform\web\server.js "%WEB_DIR%"
goto finished

:finished
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%

:failed
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%

:failed_status
popd
exit /b 1

:missing_linux
echo No linux build for "%PROJECT%" at build\%PROJECT%\linux\%PROJECT%. 1>&2
echo Run build.bat linux %PROJECT% first. 1>&2
goto failed_status

:find_adb
set "ADB="
for /f "delims=" %%A in ('where adb 2^>nul') do (
    if not defined ADB set "ADB=%%A"
)
if defined ADB exit /b 0
if defined ANDROID_HOME if exist "%ANDROID_HOME%\platform-tools\adb.exe" (
    set "ADB=%ANDROID_HOME%\platform-tools\adb.exe"
    exit /b 0
)
if defined ANDROID_SDK_ROOT if exist "%ANDROID_SDK_ROOT%\platform-tools\adb.exe" (
    set "ADB=%ANDROID_SDK_ROOT%\platform-tools\adb.exe"
    exit /b 0
)
if exist "%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe" (
    set "ADB=%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe"
    exit /b 0
)
echo adb was not found. Install Android platform-tools or set ANDROID_HOME. 1>&2
exit /b 1

:invalid_platform
echo Unknown platform "%PLATFORM%". Choose linux, windows, android, or web. 1>&2
goto invalid_usage

:invalid_project
echo Unknown Omni project "%PROJECT%". 1>&2
echo Available projects: 1>&2
call :list_projects 1>&2
goto invalid_usage

:invalid_usage
call :show_usage 1>&2
exit /b 2

:usage
call :show_usage
exit /b 0

:show_usage
echo Usage: run.bat [linux^|windows^|android^|web] [project]
echo A project is a directory under projects\ with an entry file named ^<project^>.omni.
echo Available projects:
call :list_projects
echo If either argument is omitted, the script prompts for it.
echo The script launches the existing build in build\^<project^>\^<platform^>.
echo Run build.bat first if that build does not exist.
echo The web option serves that directory.
echo The android option installs the debug APK and starts it.
exit /b 0

:list_projects
for /d %%D in ("%ROOT_DIR%projects\*") do (
    if exist "%%D\%%~nxD.omni" echo   %%~nxD
)
exit /b 0

:prompt_project
echo Available projects:
call :list_projects
set /p "PROJECT=Omni project: "
exit /b 0

:validate_project
if not defined PROJECT exit /b 1
for /f "delims=ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-" %%A in ("%PROJECT%") do exit /b 1
echo.%PROJECT:~0,1%| findstr /r "[A-Za-z0-9]" >nul || exit /b 1
if not exist "%ROOT_DIR%projects\%PROJECT%\%PROJECT%.omni" exit /b 1
exit /b 0
