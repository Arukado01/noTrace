# Managers y utilidades reutilizables

**Autor de la documentación de NO TRACE:** Carlos Cortina.

El inventario procede de la inspección de `Engine/Include/Managers`, sus implementaciones y [EngineContext.h](../Engine/Include/Core/EngineContext.h). Las aplicaciones en NO TRACE son propuestas de integración, no sistemas ya implementados. El ciclo de escenas se explica en [ARQUITECTURA.md](ARQUITECTURA.md).

## Inventario existente

| Componente | Responsabilidad verificada | Aplicación prevista en NO TRACE |
| --- | --- | --- |
| `SceneManager` | Solicitar cambios y reinicio de escena | Menú, base, misión y resultados |
| `InputManager` | Vincular acciones enteras a teclado, ratón, botones y ejes de mando; consultar estado | Movimiento y acciones contextuales |
| `GuiManager` | Añadir y retirar widgets TGUI; procesar eventos y dibujar la interfaz | Menús y paneles |
| `CursorManager` | Dibujar cursor propio y obtener o establecer posición; permitir movimiento con mando | Navegación de interfaz |
| `RenderManager` | Dibujar en textura, aplicar posprocesado, establecer y restablecer `sf::View` | Escenarios y cámara inicial |
| `ResourceManager` | Cachear texturas, buffers de sonido y fuentes; abrir música | Recursos visuales y sonoros |
| `AudioManager` | Ajustar volumen global y posición y dirección del oyente SFML | Audio del juego |
| `TimeManager` | Proporcionar delta limitado y tiempo transcurrido | Movimiento y simulación por frame |
| `SaveManager` | Leer y escribir valores JSON en `Content/Save.json` | Ajustes y progreso futuros |
| `RandomManager` | Generar números, posiciones, ángulos y colores aleatorios | Variación de escenarios |
| `ScreenshotManager` | Guardar capturas de la ventana en `Content/Screenshots` | Revisión manual y depuración |
| `ClipboardManager` | Leer y escribir texto del portapapeles | Posible ayuda con datos de conexión futuros |

## Utilidades y registro

`Cooldown` encapsula un temporizador basado en `sf::Clock` con inicio, parada, reinicio, duración y consulta de tiempo. Los ejemplos usan `OnPause` para detenerlo y reanudarlo; no debe suponerse una pausa automática de todo temporizador por pertenecer a una escena.

`SceneUtils.h` contiene utilidades de distancia, límites de ventana, intersección de rectángulos de formas SFML y otras operaciones geométricas simples. `Bounce` detecta y resuelve sus colisiones en la propia escena. Estas funciones no constituyen una física general ni colisiones con un mapa.

`Utils/Log.h` ofrece `LOG_INFO`, `LOG_WARNING` y `LOG_ERROR` sobre spdlog. `VERIFY` evalúa una expresión y la comprueba con una aserción en compilaciones que la mantienen activa. Son mecanismos existentes para registro y comprobación; no hay un manager de depuración adicional.

## Límites que afectan a la integración

- `InputManager::Pressed` consulta si alguna entrada vinculada está pulsada; no distingue el instante inicial de la pulsación. Para acciones únicas se podrá utilizar `Scene::OnEvent` o un estado propio.
- El motor limpia los enlaces de entrada al cambiar de escena. Cada escena debe establecer los suyos en `Start`.
- `GuiManager` conserva widgets hasta que se retiran explícitamente. La escena deberá gestionar su grupo y retirarlo en `OnCleanup` cuando corresponda.
- `RenderManager` no restablece automáticamente la vista en una transición. La escena que use una cámara deberá gestionar `SetView` y `ResetView`.
- El volumen y la orientación globales del oyente pertenecen a `AudioManager`; las instancias de sonidos y música y su ciclo de vida están en las escenas de ejemplo.
- Los recursos usan rutas relativas a `Content`; algunas cargas pueden devolver punteros nulos u opcionales vacíos. La futura integración deberá respetar esos resultados y su vida útil.

## Criterios para reutilizar y ampliar

Antes de crear un sistema, se verificará si uno de estos componentes ya cubre su responsabilidad. Se accederá a los servicios existentes mediante `ctx`, evitando implementar otro cargador de recursos, otra GUI, otro sistema de entrada o transiciones paralelas.

No existen managers dedicados a cámara, físicas, colisiones de mapa, entidades, navegación, misiones ni red en las fuentes auditadas. Para una cámara inicial se dispone de `RenderManager::SetView`. La geometría sólida, línea de visión y navegación necesitan diseño específico en las fases del juego que las requieran; su ausencia no justifica crear todos esos managers de antemano.

Si aparece una responsabilidad compartida que precise un manager nuevo, se documentará por qué no la resuelven los existentes, sus contratos y su integración con el contexto. Primero se buscará resolver necesidades del juego dentro de sus sistemas. Las convenciones de configuración, tipos y archivos están en [ARQUITECTURA.md](ARQUITECTURA.md); el orden y sus condiciones están en [PLAN_DE_DESARROLLO.md](PLAN_DE_DESARROLLO.md).
