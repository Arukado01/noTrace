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

Caminar, correr, agacharse y tumbarse cambian la velocidad, el ruido y la distancia a la que pueden verte. Pulsa **Z** para tumbarte: al introducir una dirección te arrastras automáticamente; al soltarla permaneces tumbado. Pulsa Z otra vez para levantarte. Los muebles sólidos bloquean el movimiento según su huella en el suelo. La cobertura baja protege a quien está agachado o tumbado; la alta interrumpe la visión. **F** permite adherirse a una cobertura alta y deslizarse por ella; mantén **Alt + dirección lateral** cerca de un extremo para asomarte y suelta para volver.

La niebla de guerra oculta enemigos, objetos e indicadores fuera de tu campo de visión o detrás de una superficie opaca. Los pasos de una patrulla cercana pueden producir pistas breves y aproximadas en el suelo, sin revelar su cuerpo, identidad ni equipo. Los personajes utilizan sprites 2D de pixel art con fotogramas de movimiento.

Los guardias distinguen patrulla, sospecha, investigación, alerta y búsqueda. Sus conos de observación, los obstáculos y tu postura afectan a la detección. Un ruido los atrae a su origen; no les revela automáticamente dónde estás. Después de perderte buscan en la última posición que pudieron observar.

Mantén **G** para preparar un señuelo: la guía muestra su recorrido y destino, hasta 340 unidades, limitado por obstáculos y bordes. Suelta G para lanzarlo; clic derecho o Escape cancela. Pulsa **Q** mirando hacia una pared cercana para golpearla y atraer a los guardias que puedan oírla. Abre puertas, recupera archivos y usa herramientas para desactivar cámaras o sabotear la central. El repetidor necesita agentes próximos: en cooperación se opera más rápido; también puede completarse individualmente.

Los botes de basura y armarios compatibles admiten un único ocupante: agente o cuerpo neutralizado. **E** entra y sale por un acceso libre; dentro no puedes moverte ni disparar. Las mesas señaladas permiten ocultarte tumbado y no almacenan cuerpos. Una patrulla no descubre un cuerpo almacenado simplemente por pasar junto al contenedor.

## CQC y armas

Acércate a un guardia y pulsa **R** en cuatro acciones separadas: agarre, derribo, inmovilización e incapacitación. Apunta con el cursor táctico a cabeza, torso o extremidades: un impacto letal en la cabeza elimina inmediatamente y uno no letal deja inconsciente. Los impactos en otras zonas acumulan daño o aturdimiento. Cada disparo consume munición y queda registrado aunque no impacte; las superficies sólidas detienen su trayectoria según su altura.

Los guardias que te detectan pueden disparar cuando tienen línea de visión y atacar cuerpo a cuerpo a corta distancia. Estar tumbado evita el daño directo de sus balas; sus ataques cuerpo a cuerpo siguen siendo peligrosos. Su munición es finita; al agotarla intentan acercarse para atacar. Los iconos distinguen sospecha, detección confirmada, combate e inconsciencia cuando puedes ver al guardia. El HUD muestra únicamente la vida y la resistencia de tu agente. Correr y ejecutar CQC consumen resistencia; descansar permite recuperarla. La salud agotada por daño letal elimina al agente. La incapacitación temporal permite recuperarse o recibir ayuda de un compañero.

Mantén **E** cerca de un guardia neutralizado para arrastrar un solo cuerpo y suelta para dejarlo. Acércalo a un contenedor vacío compatible y pulsa E para almacenarlo. Las paredes y los muebles bloquean tanto al agente como al cuerpo. Las patrullas reaccionan a los cuerpos expuestos que encuentran. En Doble filo un agente incapacitado puede ser reanimado por un compañero con E; incapacitar no satisface la orden de eliminar.

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
| Arrastrarse tumbado | Moverse tras tumbarse con Z |
| Interactuar / extraerse / reanimar | E |
| CQC por etapas | R |
| Apuntar | Cursor táctico |
| Acercar / alejar la vista | Rueda del ratón |
| Disparo letal / no letal | Clic izquierdo / derecho |
| Preparar / lanzar señuelo | Mantener / soltar G |
| Cancelar preparación del señuelo | Clic derecho o Escape |
| Golpear pared cercana | Q |
| Arrastrar / soltar cuerpo | Mantener / soltar E |
| Entrar / salir de un escondite o almacenar cuerpo | E contextual |
| Adherirse / salir de cobertura alta | F |
| Asomarse desde un extremo de cobertura | Alt + dirección lateral |
| Herramienta contextual | T |
| Mostrar orden privada | Tab |
| Sala de despliegue en la base | F2 |
| Pausa local | Escape |
| Reanudar, reiniciar, opciones, base, menú o salir | Menú propio de pausa |
| Captura | F12 |
| Alternar pantalla completa | F11 o Alt + Enter |

En una partida individual la pausa detiene la simulación. En LAN el mundo continúa para los demás; tu agente deja de recibir órdenes de movimiento mientras tienes la pausa abierta o la ventana sin foco. El anfitrión controla el reinicio de una operación y el regreso del equipo a la base. El zoom solo cambia tu vista y conserva la puntería sobre el mundo.

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
