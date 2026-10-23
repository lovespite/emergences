@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
set "BUILD_EXIT_CODE=%ERRORLEVEL%"
echo.
rem Keep double-click output visible; parameterized or CI calls do not pause.
if not defined CI if "%~1"=="" pause
exit /b %BUILD_EXIT_CODE%
