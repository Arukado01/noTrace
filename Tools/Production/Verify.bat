@echo off
chcp 65001 >nul
call "%~dp0..\..\.vscode\build-environment.bat"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Verify.ps1" %*
exit /b %errorlevel%
