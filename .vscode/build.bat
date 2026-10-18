@echo off
chcp 65001 >nul
rem Unifica la codificacion de /showIncludes con el prefijo UTF-8 que compara Ninja.
rem Entorno de build para VS Code: carga MSVC y compila con CMake+Ninja.
rem Uso: build.bat [argumentos extra de cmake --build, p. ej. --clean-first]
rem Un build correcto de desarrollo genera y publica una version Release independiente.

call "%~dp0build-environment.bat"
if errorlevel 1 exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\Tools\Production\Build.ps1" %*
exit /b %errorlevel%
