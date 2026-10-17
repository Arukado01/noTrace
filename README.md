# NO TRACE

**Carlos Cortina · CIO — Clandestine Intelligence Organization**

Infiltración táctica en pixel art axonométrico sobre Awesome Engine. Prepara una operación, observa a las patrullas, improvisa una ruta y alcanza la extracción. Puedes jugar individualmente o crear una sala por red local para hasta cuatro agentes.

## De la base a la operación

«Nueva operación» abre el acceso a CIO: identifica a tu agente y elige una partida individual, crea una sala LAN o conecta mediante la IP del anfitrión.

La base tiene cuatro estaciones recorribles. Acércate y pulsa **E**:

- **Operaciones:** el anfitrión selecciona el contrato.
- **Armería:** equipo estándar, infiltración sin armas, no letal o sabotaje. Munición, señuelos y herramientas son finitos.
- **Inteligencia:** objetivos, riesgos y reglas de sigilo.
- **Despliegue:** sala de espera, preparación de cada agente e inicio de la operación. **F2** permite abrir esta sala desde la base.

En LAN todos deben marcarse preparados antes de desplegar. Cambiar el contrato o el equipo exige volver a prepararse. Al finalizar aparece el informe de misión; el anfitrión puede regresar con el equipo a la base y comenzar otra operación.

## Contratos

| Contrato | Objetivo y condiciones |
| --- | --- |
| Archivo fantasma | Recuperar el archivo principal y extraerse. La fuerza letal tiene una penalización elevada. |
| Relevo silencioso | Recuperar el archivo, sabotear el repetidor y extraerse. Una alarma confirmada cancela el contrato. |
| Doble filo | Recuperar el archivo y extraerse. Con varios agentes, uno recibe una orden de traición. La fuerza letal está permitida, con consecuencias para el sigilo. |

El archivo secundario y el repetidor pueden añadir recompensas u órdenes privadas. Cada operación combina módulos de obstáculos, variaciones de patrullas y una semilla nueva. Los corredores exteriores ofrecen accesos alternativos.

## Sigilo y entorno

Caminar, correr, agacharse y tumbarse cambian la velocidad, el ruido y la distancia a la que pueden verte. **Tumbarse no desplaza al agente:** mantén X mientras te mueves para arrastrarte. La cobertura baja protege a quien está agachado o tumbado; la alta interrumpe la visión. Solo los escondites señalados permiten ocultarse explícitamente.

Los guardias distinguen patrulla, sospecha, investigación, alerta y búsqueda. Sus conos de observación, los obstáculos y tu postura afectan a la detección. Un ruido los atrae a su origen; no les revela automáticamente dónde estás. Después de perderte buscan en la última posición que pudieron observar.

Lanza señuelos, golpea una pared, abre puertas, recupera archivos y usa herramientas para desactivar cámaras o sabotear la central. El repetidor necesita agentes próximos: en cooperación se opera más rápido; también puede completarse individualmente.

## CQC y armas

Acércate a un guardia y pulsa **Q** en cuatro acciones separadas: agarre, derribo, inmovilización e incapacitación. Las armas letales eliminan; las no letales incapacitan. Apunta con el cursor táctico. Cada disparo consume munición y queda registrado aunque no impacte.

Pulsa **B** cerca de un guardia neutralizado para arrastrarlo; pulsa **E** junto a un escondite para ocultar el cuerpo. Las patrullas reaccionan a los cuerpos que encuentran. En Doble filo un agente incapacitado puede ser reanimado por un compañero con E; incapacitar no satisface la orden de eliminar.

## Órdenes privadas y traición

**Tab** muestra únicamente tu orden privada: obtener una copia adicional, recuperar un dispositivo o cumplir la orden de traición de Doble filo. Las órdenes de otros agentes no se envían a tu cliente.

El traidor gana si elimina a todos los demás agentes sin activar alarmas. El equipo gana al cumplir el contrato y extraerse. El traidor comparte las reglas físicas y de percepción: sus disparos, ruidos y cuerpos pueden comprometer su propia operación. Las otras modalidades no asignan traidor.

## Informe y rango Maestro

Los rangos son **D, C, B, A, S, S+ y S++ — Maestro**. El registro de la operación conserva objetivos, extracción, detecciones, alarmas, disparos, CQC y cuerpos encontrados. La evaluación no depende únicamente del tiempo.

Maestro exige completar los objetivos obligatorios, extraerse y terminar sin armas letales o no letales, CQC, detecciones visuales confirmadas ni alarmas por descubrimiento del agente. Una detección invalida Maestro aunque después escapes. Las distracciones y las investigaciones de ruido sin descubrir al agente están permitidas.

## Controles

| Acción | Control |
| --- | --- |
| Moverse | WASD o flechas |
| Correr | Mayús |
| Agacharse / levantarse | C |
| Tumbarse / levantarse | Z |
| Arrastrarse tumbado | Mantener X y mover |
| Interactuar / extraerse / reanimar | E |
| CQC por etapas | Q |
| Apuntar | Cursor táctico |
| Disparo letal / no letal | Clic izquierdo / derecho |
| Lanzar señuelo | G |
| Golpear pared | K |
| Arrastrar / soltar cuerpo | B |
| Herramienta contextual | T |
| Mostrar orden privada | Tab |
| Sala de despliegue en la base | F2 |
| Pausa local | Escape |
| Volver al menú | M |
| Captura | F12 |
| Alternar pantalla completa | F11 o Alt + Enter |

En una partida individual la pausa detiene la simulación. En LAN el mundo continúa para los demás; tu agente deja de recibir órdenes de movimiento mientras tienes la pausa abierta o la ventana sin foco.

## Pantalla y sonido

El menú conserva el logotipo, el icono y el cursor propios. La portada estática muestra dos agentes y utiliza una paleta de 16 colores. «Opciones» permite seleccionar resoluciones reales, ventana o pantalla completa, volumen y velocidad del cursor con mando. Los cambios de pantalla requieren confirmación; si no los conservas se revierte la configuración anterior. Los ajustes se guardan localmente.

## Jugar por LAN

1. Usa la misma versión del juego en los equipos y conéctalos a la misma red local.
2. El anfitrión elige **Crear sala LAN**. La sala muestra su IP y puerto; el predeterminado es **55031/TCP**.
3. Los demás eligen **Conectar por IP** e introducen esos datos.
4. Permite el tráfico del juego en la red privada cuando Windows lo solicite.
5. Prepara el equipo, marca cada agente como preparado y despliega desde el anfitrión.

La implementación tiene pruebas automáticas de transporte y sesión. **La validación en dos computadores físicos sigue pendiente**: las pruebas locales entre procesos no la sustituyen.

## Ejecutar

Desde la raíz del proyecto, con las herramientas C++ configuradas:

```powershell
cmd /c .vscode\build.bat --parallel 2
.\build\AwesomeEngine.exe
```

El motor conserva Bounce y Clicker para desarrollo mediante `--scene Bounce` y `--scene Clicker`; no aparecen como opciones del menú de NO TRACE. La documentación técnica y los registros de pruebas se conservan localmente en `docs/`.
