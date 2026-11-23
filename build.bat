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

if /I "%PLATFORM%"=="linux" goto build_linux
if /I "%PLATFORM%"=="windows" goto build_windows
if /I "%PLATFORM%"=="android" goto build_android
goto build_web

:build_linux
cmake -S platform/linux -B build\%PROJECT%\linux -DALLWHITHER_PROJECT=%PROJECT%
if errorlevel 1 goto failed
cmake --build build\%PROJECT%\linux
if errorlevel 1 goto failed
echo Built build\%PROJECT%\linux\%PROJECT%
goto succeeded

:build_windows
dotnet build platform/windows/Allwhither.Windows.csproj --artifacts-path build\%PROJECT%\windows /p:EnableWindowsTargeting=true /p:AllwhitherProject=%PROJECT%
if errorlevel 1 goto failed
echo Built build\%PROJECT%\windows
goto succeeded

:build_android
if not defined ANDROID_HOME if exist "%LOCALAPPDATA%\Android\Sdk\platforms" set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
if not defined ANDROID_HOME if exist "%USERPROFILE%\Android\Sdk\platforms" set "ANDROID_HOME=%USERPROFILE%\Android\Sdk"
if not defined ANDROID_SDK_ROOT set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
call platform\android\gradlew.bat -p platform\android assembleDebug -PallwhitherProject=%PROJECT%
if errorlevel 1 goto failed
echo Built build\%PROJECT%\android
goto succeeded

:build_web
set "WEB_DIR=build\%PROJECT%\web"
if exist "%WEB_DIR%" rmdir /s /q "%WEB_DIR%"
mkdir "%WEB_DIR%" || goto failed
for %%F in ("%ROOT_DIR%platform\web\*") do (
    if /I not "%%~nxF"=="server.js" (
        copy /y "%%F" "%WEB_DIR%\" >nul || goto failed
    )
)
python tools\embed_resources.py --platform json --project-dir "projects\%PROJECT%" --output "%WEB_DIR%\payload.json"
if errorlevel 1 goto failed
if defined PORT (set "WEB_PORT=%PORT%") else set "WEB_PORT=8080"
echo Built %WEB_DIR%
echo Serving http://127.0.0.1:%WEB_PORT%/
node platform\web\server.js "%WEB_DIR%"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%

:succeeded
popd
exit /b 0

:failed
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%

:invalid_platform
echo Unknown platform "%PLATFORM%". Choose linux, windows, android, or web. 1>&2
goto invalid_usage

:invalid_project
echo Unknown Allwhither project "%PROJECT%". 1>&2
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
echo Usage: build.bat [linux^|windows^|android^|web] [project]
echo A project is a directory under projects\ with an entry file named ^<project^>.json.
echo Available projects:
call :list_projects
echo If either argument is omitted, the script prompts for it.
echo Each build is written to build\^<project^>\^<platform^>.
echo The web option then serves that directory.
exit /b 0

:list_projects
for /d %%D in ("%ROOT_DIR%projects\*") do (
    if exist "%%D\%%~nxD.json" echo   %%~nxD
)
exit /b 0

:prompt_project
echo Available projects:
call :list_projects
set /p "PROJECT=Allwhither project: "
exit /b 0

:validate_project
if not defined PROJECT exit /b 1
for /f "delims=ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-" %%A in ("%PROJECT%") do exit /b 1
echo.%PROJECT:~0,1%| findstr /r "[A-Za-z0-9]" >nul || exit /b 1
if not exist "%ROOT_DIR%projects\%PROJECT%\%PROJECT%.json" exit /b 1
exit /b 0
