@echo off
rem Comparte MSVC y la codificacion de /showIncludes entre el build y su verificacion.
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>&1 || (
    echo [AwesomeEngine] ERROR: no se pudo cargar el entorno de MSVC ^(vcvars64.bat^).
    exit /b 1
)
where cmake >nul 2>&1 || set "PATH=%PATH%;C:\Users\desar\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.4-windows-x86_64\bin"
where ninja >nul 2>&1 || set "PATH=%PATH%;C:\Users\desar\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe"
exit /b 0
