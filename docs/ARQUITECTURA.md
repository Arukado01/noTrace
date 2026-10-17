# Arquitectura de Awesome Engine

**Autor de la documentación de NO TRACE:** Carlos Cortina.

Este documento conserva los hallazgos técnicos de la Fase 0. Describe el código inspeccionado; las decisiones para integrar NO TRACE se identifican como trabajo futuro. El diseño del videojuego está en el [README principal](../README.md).

## Estructura real

```text
AwesomeEngine/
├── CMakeLists.txt                 Compilación y dependencias
├── Content/                       Config.json, texturas, fuentes, temas, audio y shaders
├── Engine/
│   ├── Include/Core/              Motor, contexto, configuración, eventos y pausa
│   ├── Include/Managers/          Servicios comunes
│   ├── Include/Scene/             Contrato, fábrica y utilidades de escenas
│   ├── Include/Graphics/          Efectos de posprocesado
│   ├── Include/utils/             Entrada, temporizador, registro y verificaciones
│   └── Source/                    Implementaciones y Main.cpp
├── Games/
│   ├── Include/Config/            Constantes de los juegos de ejemplo
│   ├── Include/Types/             Acciones y estructuras de los ejemplos
│   ├── Include/                   Declaraciones de escenas
│   └── Source/                    Implementaciones de escenas
├── docs/                          Documentación técnica
└── build/                         Compilación local ignorada por Git
```

## Punto de entrada y bucle principal

[Main.cpp](../Engine/Source/Main.cpp) construye `Engine` y repite procesamiento de eventos, actualización y dibujo mientras la ventana esté abierta. Si pierde el foco, espera brevemente y omite actualización y dibujo en esa iteración.

[Engine.cpp](../Engine/Source/Core/Engine.cpp) crea la ventana a partir de `gConfig`, construye `EngineContext`, obtiene las escenas desde `SceneFactory::CreateScenes` y prepara el overlay de pausa. El contexto reúne los servicios descritos en [MANAGERS.md](MANAGERS.md).

El motor conserva las escenas en un mapa de `std::unique_ptr<Scene>`; el puntero a la escena activa no posee esa memoria. Las escenas se construyen al crear la fábrica y permanecen durante la vida del motor.

## Escena inicial y diagnóstico del menú

El constructor solicita explícitamente:

```cpp
context_.scenes.ChangeScene("Clicker");
```

[SceneFactory.cpp](../Engine/Source/Scene/SceneFactory.cpp) solo registra `Bounce` y `Clicker`. No existe una escena `Menu` en las fuentes auditadas. Por ello el arranque directo en Clicker responde a la selección escrita en el código; el menú principal no se ha implementado.

`Engine::EventSceneMenuReturn` solicita `"Menu"`. Lo invocan el botón Menu del overlay, M y Select del mando. Al procesar esa transición, `EventSceneChange` exige que el nombre exista mediante una aserción y después usa `scenes_.at(name)`. Con aserciones activas se espera un fallo; sin ellas, el acceso puede lanzar una excepción. Es un diagnóstico por inspección estática, no una reproducción visual. Los procedimientos están en [PRUEBAS.md](PRUEBAS.md).

El overlay de pausa es un grupo TGUI global con Resume, Restart, Menu y Quit. Permite pausar la escena y elegir acciones globales, pero no sustituye una escena de menú principal. La corrección del arranque y del retorno al menú queda para la Fase 1.

## Contrato y transición entre escenas

[Scene.h](../Engine/Include/Scene/Scene.h) recibe un `EngineContext&` accesible como `ctx`. Exige `Start`, `Update` y `Render`; ofrece `OnEvent`, `OnPause` y `OnCleanup` opcionales.

La transición funciona así:

1. Una escena o el motor solicita `ctx.scenes.ChangeScene(nombre)`.
2. `SceneManager` guarda como máximo una solicitud pendiente; otra solicitud antes de consumirla se ignora.
3. Al comenzar `Engine::ProcessEvent`, `FetchNextScene` entrega el nombre pendiente y actualiza el nombre actual.
4. `Engine::EventSceneChange` valida el registro, llama `OnCleanup` de la escena anterior, limpia los enlaces de entrada y cambia la escena activa.
5. El motor llama `Start` en la nueva escena.

`RestartCurrentScene` solicita de nuevo el nombre actual y reutiliza el mismo proceso. `Bounce` detiene su audio en `OnCleanup`; `Clicker` no implementa limpieza específica.

Después de aplicar una transición, el motor envía cada evento a `EngineVisitor`, a TGUI y, si el overlay está oculto, a la escena. Actualiza tiempo y cursor y delega la lógica en la escena cuando no hay pausa. Dibuja primero la escena mediante `RenderManager`, después la interfaz y finalmente el cursor.

## Configuración global y del juego

[Content/Config.json](../Content/Config.json) define título y tamaño de ventana, volumen, fondo, cursor, zona muerta del mando, límite de delta y silenciamiento de mensajes SFML. `EngineConfig` lo carga al inicializar `gConfig`. No es un almacén genérico de parámetros por juego.

Los ejemplos centralizan constantes propias en `Games/Include/Config/*Config.h` y acciones y estructuras compartidas en `Games/Include/Types/*Types.h`. La declaración de cada escena está en `Games/Include/*.h` y su comportamiento en `Games/Source/*.cpp`.

Para NO TRACE se seguirá esta organización cuando existan datos que separar. Velocidades, detección, duraciones y puntuación se centralizarán en la fase que los introduzca. No se crearán archivos vacíos solo para reproducir una plantilla ni un segundo sistema de configuración global.

## Convenciones y creación de sistemas

El proyecto usa C++20, `#pragma once`, clases en PascalCase, miembros privados terminados en `_` y namespaces propios para `Bounce` y `Clicker`. `.clang-format` se basa en LLVM con sangría de dos espacios. No se observa un encabezado de autor uniforme.

Los archivos nuevos de NO TRACE identificarán a Carlos Cortina como autor mediante una línea de atribución. Al modificar archivos se conservarán metadatos y autoría históricos. Los comentarios nuevos o cambiados estarán en español, tendrán una línea como máximo y explicarán decisiones no evidentes. Las explicaciones extensas se escribirán en `docs/`.

La integración prevista consiste en declarar la escena o sistema en `Games/Include`, separar configuración y tipos cuando corresponda e implementar comportamiento en `Games/Source`. CMake descubre los `.cpp` de ambas carpetas de fuentes, mientras que el registro de escenas es explícito en la fábrica. Las escenas accederán a los servicios mediante `ctx`.

Las responsabilidades reutilizables y la gestión de estado entre escenas están en [MANAGERS.md](MANAGERS.md). Cualquier modificación necesaria de una abstracción central deberá explicar su impacto sobre los ejemplos; primero se buscará resolver la necesidad dentro del juego.

## Relación de los ejemplos con el motor

`Bounce` demuestra movimiento con delta, teclado y mando, colisiones simples, sonido, música, récord y reinicio. `Clicker` demuestra cursor, temporizadores, texturas, guardado y reinicio. Ambos dependen del motor y de recursos en `Content`; no dependen entre sí.

No existe un sistema general de entidades o controladores separado en esos ejemplos: el estado reside en estructuras de `Types` y la lógica se coordina desde la escena. La separación de control, simulación y representación de NO TRACE deberá crecer según cada fase, sin asumir infraestructura que el repositorio aún no tiene.

Las dependencias y los comandos de compilación se conservan en [PRUEBAS.md](PRUEBAS.md), y las etapas de integración en [PLAN_DE_DESARROLLO.md](PLAN_DE_DESARROLLO.md).
