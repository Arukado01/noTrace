# Plan de desarrollo de NO TRACE

**Autor:** Carlos Cortina.

La Fase 0 documenta el estado real de Awesome Engine y planifica su integración. Esta corrección reorganiza la documentación: el diseño del juego está en [README.md](../README.md), la auditoría en [ARQUITECTURA.md](ARQUITECTURA.md) y [MANAGERS.md](MANAGERS.md), y los procedimientos y evidencias en [PRUEBAS.md](PRUEBAS.md). La Fase 1 permanece pendiente de autorización.

## Reglas de avance

Se conserva el orden de fases solicitado. Ninguna implementación comenzará sin autorización expresa de Carlos Cortina. Al finalizar cada fase se actualizarán los documentos afectados, se ejecutarán las comprobaciones posibles y se informará de resultados, limitaciones y pasos de prueba manual. Si una prueba falla, se investigará y corregirá dentro del alcance antes de declarar la entrega completada. Las pruebas no ejecutadas se identificarán como tales.

Tras la entrega, Carlos Cortina realizará la prueba manual y el commit correspondiente antes de autorizar la fase siguiente. Si una dependencia técnica exige cambiar el orden, se explicará la propuesta y se esperará su aprobación.

## Entregas y criterios de aceptación

| Fase | Entrega prevista | Criterio de aceptación |
| --- | --- | --- |
| **0. Auditoría y plan** | Inspección del motor, diagnóstico del menú y documentación | Hallazgos verificables, compilación disponible registrada y documentación organizada |
| **1. Arranque y menú principal** | Registro de NO TRACE, escena inicial, menú, opciones pertinentes y transiciones | Compila, abre el menú y sus opciones implementadas funcionan sin romper los ejemplos |
| **2. Base de operaciones** | Espacio recorrible con operaciones, selección de misión, preparación y despliegue | Entrar desde el menú, moverse e interactuar con puntos funcionales |
| **3. Control del agente** | Caminar, correr, agacharse, tumbarse, arrastrarse, interacción y colisiones | Posturas y transiciones diferenciadas; parámetros de movimiento y sigilo propios; obstáculos sólidos |
| **4. Entorno e interacciones** | Coberturas, escondites compatibles, objetos lanzables, ruido, paredes, puertas y dispositivos | Interacciones comprensibles que cambian el entorno según su geometría |
| **5. Percepción e IA** | Patrulla, visión con obstáculos, audición, sospecha, investigación, alerta y búsqueda | Investigar ruido, confirmar visión, perseguir y buscar la última posición conocida; alarma según contrato |
| **6. CQC y neutralización** | Aproximación, agarre, derribo, inmovilización, incapacidad y hallazgo de cuerpos | Neutralización sin alarma automática y reacción coherente de otro guardia; incapacidad temporal distinta de eliminación |
| **7. Armas y dispositivos** | Equipo letal y no letal, distracciones, herramientas, inventario y preparación | Efectos diferenciados y eventos de uso registrados según reglas de contrato |
| **8. Misiones y puntuación** | Contratos, objetivos principales y secundarios, extracción, fracaso, eventos y rangos | Evaluar D a S++; comprobar elegibilidad de Maestro y pérdida por detección confirmada |
| **9. Traidores y órdenes secretas** | Objetivos privados y modalidad específica de traición | Verificar privacidad y victorias del equipo y traidor en una misión cooperativa |
| **10. Multijugador LAN** | Anfitrión, conexión local, sala, identidad, sincronización, validación y desconexión | Dos computadores distintos completan una misión con estados y resultados coherentes |
| **11. Escenarios modulares** | Variar salas, rutas, posiciones, objetivos y recursos configurables | Repetir una misión con variantes que conserven colisiones, navegación, visibilidad y extracción |
| **12. Integración y pulido** | Revisar el recorrido completo y corregir problemas comprobados | Ejecutar desde menú y base hasta operación, resultados y regreso según el alcance implementado |

## Dependencias y decisiones previstas

La Fase 1 debe resolver la ruta inexistente `"Menu"`, usada por el overlay y los atajos globales, y conservar la estabilidad y el acceso a los ejemplos. Usará el registro y las transiciones existentes. La base validará navegación de escenas e interacción; el control completo del agente y la geometría llegarán después.

La IA depende de posturas, ruido, cobertura y línea de visión. CQC y equipamiento añadirán eventos de acciones; el sistema de misión podrá utilizarlos para evaluación en lugar de estimar lo sucedido al final. Cada contrato determinará uso de fuerza autorizado, penalizaciones, bonificaciones y condiciones de extracción.

Para S++ — Maestro, la detección visual confirmada se registrará como un evento irreversible para la elegibilidad. Investigar un ruido sin detectar al agente no la invalidará por sí solo. Los umbrales de rangos se centralizarán y se validarán con pruebas.

Las órdenes secretas dependen de las reglas de misión. La modalidad de traidor tendrá condiciones de victoria finitas y protegerá la privacidad de esas órdenes. La LAN usará una simulación autoritativa en el anfitrión para enemigos, detecciones, colisiones relevantes, objetivos y puntuación. En la Fase 10 se evaluará TCP o UDP según las necesidades reales y se habilitará y vinculará SFML Network.

Las fases 9 y 10 completarán las pruebas cooperativas y de traición con jugadores conectados. Si para verificar la Fase 9 se descubre una dependencia que exige adelantar parte de la red, se presentará ese ajuste para aprobación antes de implementarlo; esta corrección documental no modifica el orden.

La rejugabilidad podrá combinar módulos prediseñados. No exige generación completamente aleatoria. Se prioriza una misión completa y un primer escenario cooperativo antes de ampliar el catálogo, con duración objetivo de 6 a 9 minutos ajustable tras pruebas.

## Riesgos identificados en la auditoría

| Riesgo o limitación | Acción prevista |
| --- | --- |
| Arranque fijo en Clicker y retorno a Menu inexistente | Corregir registro y rutas en Fase 1 |
| Recursos relativos a `Content` y cargas con aserciones o desreferencias | Mantener el directorio de trabajo documentado y comprobar las cargas al integrar |
| Widgets GUI y vista persistentes entre escenas | Gestionar grupos y restauración de vista en el ciclo de vida correspondiente |
| Entrada consultada por estado, sin detección de pulsación inicial | Usar eventos o estado propio cuando una acción deba ocurrir una sola vez |
| Ausencia de mapa, colisiones generales, entidades y navegación | Diseñar sistemas del juego cuando los exijan las fases 3 a 5 |
| SFML Network desactivado y ausencia de sistema de red | Habilitarlo en Fase 10 y probar entre dos equipos |
| Rutas antiguas de editor y discrepancia `Build`/`build` | Revisar entorno de desarrollo al preparar Fase 1 |
| Sin suite de tests configurada y sin prueba visual en la auditoría | Definir verificaciones pertinentes por fase y registrar lo realmente ejecutado |

## Formato de entrega de cada fase

Se usará «FASE N COMPLETADA — NO TRACE» seguido de objetivo, implementación, archivos creados y modificados, explicación técnica, managers reutilizados, compilación y pruebas, pasos manuales, resultado esperado, limitaciones, commit sugerido y confirmación para avanzar.

## Historial

| Entrega | Estado | Documentación afectada | Evidencia |
| --- | --- | --- | --- |
| Fase 0 inicial | Auditoría documentada | README raíz original | Inspección y compilación incremental con salida 0; sin prueba visual |
| Corrección documental de Fase 0 | Diseño separado de documentación técnica; pendiente de revisión de Carlos Cortina | `README.md` y cuatro documentos en `docs/` | Cinco documentos y 27 enlaces locales comprobados; `git diff --check` con salida 0 |

No se ha implementado NO TRACE ni se ha corregido el menú. El siguiente trabajo funcional es la Fase 1, pendiente de autorización y del commit correspondiente de Carlos Cortina.
