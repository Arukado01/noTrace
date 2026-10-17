@echo off
rem Entorno de build para VS Code: carga MSVC y compila con CMake+Ninja.
rem Uso: build.bat [argumentos extra de cmake --build, p. ej. --clean-first]

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>&1 || (
    echo [AwesomeEngine] ERROR: no se pudo cargar el entorno de MSVC ^(vcvars64.bat^).
    exit /b 1
)

rem Respaldo: si winget agrego cmake/ninja al PATH del usuario pero VS Code
rem arranco antes de la instalacion, se agregan manualmente.
where cmake >nul 2>&1 || set "PATH=%PATH%;C:\Users\desar\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.4-windows-x86_64\bin"
where ninja >nul 2>&1 || set "PATH=%PATH%;C:\Users\desar\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe"

cmake --build "%~dp0..\build" %*
