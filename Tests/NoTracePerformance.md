# Medición aislada de NO TRACE en Windows

Habilitar el objetivo opcional con `cmake -S . -B build -DNO_TRACE_BUILD_PERFORMANCE=ON` dentro del entorno MSVC de `.vscode/build-environment.bat`. Compilar únicamente `NoTracePerformanceTests` con `cmake --build build --target NoTracePerformanceTests --parallel 2`. El objetivo queda fuera de la compilación normal y no se publica con el juego.

Desde la raíz, ejecutar `build/NoTracePerformanceTests.exe --output build/perf-nuevo --frames 180 --warmup 24 --moving`. La salida debe ser nueva. `--fps` cambia a primera persona y `--stress` duplica los guardias e incorpora objetos adicionales. Cada ejecución copia Content, fija un guardado aislado y exporta `result.json`, `progress.json` y una captura final.

No ejecutar compiladores, otras pruebas gráficas ni montajes de imágenes durante la medición. Usar el mismo equipo, configuración, recursos, tamaño, calentamiento, número de cuadros y opciones para comparar versiones. Un nivel histórico diferente sólo proporciona contexto.

Opciones diagnósticas:

- `--profile`: activa tiempos internos inclusivos; no sumar padres e hijos. Comparar FPS principales con esta opción desactivada.
- `--real-time`: usa el delta real de TimeManager, incluido su límite de configuración; por defecto se usa un delta nominal de 1/60 para controlar la carga simulada.
- `--gpu-fence`: añade glFinish como diagnóstico separado; no mide porcentaje de uso ni VRAM de GPU.
- `--scenario warehouse`, `multilevel`, `camera10` o `top23`: posiciones iniciales comprobadas físicamente dentro del nivel real. Almacén y multinivel disparan una bala sin silenciador al comenzar las muestras; cámara conserva su barrido y detección reales.
- `--width` y `--height`: tamaño nativo de ventana; el espacio lógico depende de WindowManager y la configuración.

El estrés usa guardias adicionales en posiciones libres y objetos Hideout de logística. Las propiedades efectivas de esos objetos incluyen colisión y oclusión. No representa una ampliación del nivel de producción.

El arnés ejecuta Game, simulación, render, GUI y presentación reales, pero omite las otras escenas, la orquestación completa de Engine y el cursor. La memoria es la del proceso del arnés. La carga inicial y la copia de Content quedan fuera de las muestras; los primeros usos diferidos pueden aparecer dentro. La orden privada se asigna durante el despliegue real antes de fijar la semilla del mundo y su texto puede variar entre capturas.

FPS medio significa 1000/media de cuadro; FPS mínimo significa 1000/máximo cuadro observado, no un mínimo sostenido. CPU se expresa como tiempo del proceso y núcleos equivalentes; los datos de OpenGL identifican el dispositivo empleado. Los contadores sin llamadas no demuestran coste cero de un subsistema sin instrumentar.
