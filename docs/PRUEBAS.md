# Compilación, ejecución y pruebas

**Autor de la documentación de NO TRACE:** Carlos Cortina.

Este documento distingue procedimientos reproducibles, resultados históricos y comprobaciones pendientes. La arquitectura inspeccionada está en [ARQUITECTURA.md](ARQUITECTURA.md); los criterios de aceptación por fase, en [PLAN_DE_DESARROLLO.md](PLAN_DE_DESARROLLO.md).

## Requisitos y dependencias

[CMakeLists.txt](../CMakeLists.txt) exige CMake 3.28 o posterior y C++20. Descubre las fuentes de `Engine/Source` y `Games/Source` y genera el ejecutable `AwesomeEngine`. Se necesita un compilador compatible y, para los comandos siguientes, Ninja disponible.

Las dependencias declaradas con `FetchContent` son:

| Dependencia | Versión declarada |
| --- | --- |
| SFML | 3.1.0 |
| nlohmann/json | 3.12.0 |
| spdlog | 1.16.0 |
| TGUI | 1.13.0 |
| magic_enum | 0.9.8 |

Se vinculan `SFML::Graphics`, `SFML::Audio`, JSON, spdlog, TGUI y magic_enum. `SFML_BUILD_NETWORK` está desactivado. La primera configuración puede necesitar acceso a Internet para descargar dependencias; una caché local puede evitar nuevas descargas.

## Compilar y ejecutar

Desde PowerShell en la raíz del repositorio:

```powershell
cmake -S . -B build -G Ninja
cmake --build build --parallel 2
.\build\AwesomeEngine.exe
```

Ejecutar desde la raíz es necesario porque configuración, fuentes, tema, texturas y audio se buscan mediante rutas relativas `Content/...`. Si `build` ya tiene otro generador o compilador, debe respetarse esa configuración o usar una carpeta de compilación distinta.

En la auditoría se encontró `build/` configurado con Ninja, MSVC y Debug. CMake no estaba en `PATH`; se usó su instalación local por ruta absoluta. `.vscode` apunta a `Build`, y `.clangd` contiene rutas antiguas de otro equipo. Estas observaciones describen ese entorno; no son requisitos portables.

## Resultados realmente comprobados en la Fase 0 inicial

Se inspeccionaron fuentes, CMake, configuración, árbol local de compilación, archivos de editor y licencia. Antes de las ediciones de la auditoría original, `git status --short` estaba limpio.

La compilación incremental ejecutada fue:

```powershell
& 'C:\Users\desar\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.4-windows-x86_64\bin\cmake.exe' --build build --parallel 2
```

Terminó con **código 0** y `ninja: no work to do`. Comprueba que el árbol y los artefactos estaban al día para esa configuración. No demuestra una compilación limpia desde cero. `git diff --check` también terminó correctamente tras corregir espacios finales en la documentación.

No se ejecutó la ventana del juego para verificación visual ni se probó red. `CMakeLists.txt` no configura una suite de tests. Estos resultados son históricos: la reorganización documental no constituye una nueva prueba del ejecutable.

## Procedimiento manual del estado actual

1. Abrir PowerShell en la raíz del repositorio.
2. Compilar con `cmake --build build --parallel 2`; si CMake no está en `PATH`, usar la ruta absoluta anterior o la instalación disponible.
3. Ejecutar `.\build\AwesomeEngine.exe` desde esa misma carpeta.
4. Comprobar la escena inicial. Por inspección del código se espera Clicker; todavía no hay un menú de NO TRACE.
5. En Clicker, usar clic izquierdo o botón sur del mando sobre el objetivo. R solicita reinicio; Escape o Start del mando alternan la pausa; F12 guarda una captura; cerrar la ventana sale.
6. Registrar el comportamiento observado, la configuración usada y cualquier error. Los pasos anteriores son un procedimiento pendiente de prueba visual en esta auditoría.

M, Select y el botón Menu solicitan una escena inexistente. El fallo esperado está diagnosticado en [ARQUITECTURA.md](ARQUITECTURA.md); no deben tratarse como navegación funcional. Si se reproduce el defecto, hacerlo tras guardar trabajo y registrar si actuó la aserción o se produjo una excepción.

## Comprobaciones de la corrección documental

La corrección se limita a `README.md` y `docs/`. Se comprobó que:

- El README cubra identidad, concepto, bucle, movimiento, entorno, enemigos, combate, rangos, cooperación, base, misiones, LAN y alcance como diseño previsto.
- La auditoría y planificación estén repartidas por responsabilidad entre los cuatro documentos técnicos.
- Los enlaces Markdown a archivos locales resuelvan a destinos existentes.
- `git diff --check` no informe errores de formato.
- El conjunto de cambios permanezca dentro de la documentación solicitada.

Resultado: se revisaron los cinco documentos, sus **27 enlaces locales** resolvieron a destinos existentes y no se encontraron espacios finales. `git diff --check` terminó con código **0**. La revisión de contenido confirmó los requisitos de diseño y la conservación de los hallazgos técnicos. El estado de Git solo muestra el README modificado y la carpeta `docs/` nueva.

No se volvió a compilar ni ejecutar el juego durante esta corrección documental; no se modificaron fuentes, recursos o configuración.

## Validación de futuras fases

Cada fase documentará sus comandos, configuración, resultado real, pruebas manuales y limitaciones. Una prueba no realizada no se presentará como aprobada. Los ejemplos Bounce y Clicker servirán para comprobar regresiones cuando haya cambios funcionales; el acceso a ambos deberá quedar resuelto en la integración del menú.

La futura Fase 10 necesitará una partida entre **dos computadores distintos** conectados a la misma red local. Una prueba únicamente en localhost no demuestra que la LAN funcione. Las instrucciones para conectar jugadores se escribirán cuando exista esa funcionalidad.
