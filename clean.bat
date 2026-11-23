@echo off
setlocal

set "ROOT_DIR=%~dp0"

if exist "%ROOT_DIR%build" (
    rmdir /s /q "%ROOT_DIR%build"
    if errorlevel 1 exit /b 1
)
if exist "%ROOT_DIR%platform\linux\build" (
    rmdir /s /q "%ROOT_DIR%platform\linux\build"
    if errorlevel 1 exit /b 1
)
if exist "%ROOT_DIR%platform\windows\bin" (
    rmdir /s /q "%ROOT_DIR%platform\windows\bin"
    if errorlevel 1 exit /b 1
)
if exist "%ROOT_DIR%platform\windows\obj" (
    rmdir /s /q "%ROOT_DIR%platform\windows\obj"
    if errorlevel 1 exit /b 1
)
if exist "%ROOT_DIR%platform\android\build" (
    rmdir /s /q "%ROOT_DIR%platform\android\build"
    if errorlevel 1 exit /b 1
)
if exist "%ROOT_DIR%platform\android\app\build" (
    rmdir /s /q "%ROOT_DIR%platform\android\app\build"
    if errorlevel 1 exit /b 1
)

echo Build outputs removed.
