# NO TRACE

**Carlos Cortina · CIO — Clandestine Intelligence Organization**

Infiltración táctica en pixel art axonométrico sobre Awesome Engine. Prepara una operación, observa a las patrullas, improvisa una ruta y alcanza la extracción. Puedes jugar individualmente o crear una sala por red local para hasta cuatro agentes.

## De la base a la operación

«Nueva operación» abre el acceso a CIO: identifica a tu agente y elige una partida individual, crea una sala LAN o conecta mediante la IP del anfitrión.

La base tiene cuatro estaciones recorribles. Acércate y pulsa **E**:

- **Operaciones:** el anfitrión selecciona el contrato.
- **Armería:** equipo estándar, infiltración sin armas, no letal o sabotaje. El equipo preparado para la misión conserva munición, señuelos y herramientas finitos.
- **Inteligencia:** objetivos, riesgos y reglas de sigilo.
- **Despliegue:** sala de espera, preparación de cada agente e inicio de la operación. **F2** permite abrir esta sala desde la base.

En LAN todos deben marcarse preparados antes de desplegar. Cambiar el contrato o el equipo exige volver a prepararse. Al finalizar aparece el informe de misión; el anfitrión puede regresar con el equipo a la base y comenzar otra operación.

## Entrenamiento seguro

La base y sala de espera es un escenario recorrible de **1900 × 1060**: las estaciones de preparación están al oeste y el campo de prácticas al este. Puedes moverte, usar posturas, coberturas y escondites, probar las armas y CQC, lanzar señuelos, generar ruido y resolver los dispositivos. La niebla y los obstáculos conservan sus reglas. No aparecen guardias enemigos, patrullas ni refuerzos en esta zona.

El campo tiene **tres dianas** con aspecto propio, indicador de salud y contador de impactos. Reciben disparos, golpes, agarres, sometimientos, explosiones y empujones; su salud visible se detiene en **1** y nunca mueren, quedan inconscientes ni desaparecen por daño. Mantén **E** para practicar su arrastre aun estando activas; suelta para dejarlas. Las dianas no se guardan dentro de contenedores.

Los jugadores también permanecen vivos y operativos ante disparos de compañeros, explosiones y sometimientos. El anfitrión aplica esta protección al resolver el daño y las rutas de incapacitación. Las cámaras muestran su progreso de detección, pero durante las prácticas no activan una alarma de misión ni solicitan guardias.

Puedes practicar con una cámara, su panel, un explosivo, una cerradura, un computador, un armario, un contenedor y una mesa. El explosivo de prácticas tiene **8 segundos** de cuenta. Acércate a la consola **REARMAR PRUEBAS** situada al inicio del campo y pulsa E para restaurar dispositivos, cámaras y dianas y repetir los ejercicios. Los escondites ocupados conservan sus agentes; una puerta ocupada permanece abierta al rearmar.

En entrenamiento se reponen munición, señuelos y herramientas. El total de cada tipo de munición vuelve a **12**, pero sus cargadores de cuatro cartuchos se vacían y exigen una recarga real. El equipo elegido en armería se guarda por separado: practicar con armas no añade munición a una misión preparada sin ellas. Al desplegar regresan los guardias, el daño normal, las alarmas y las condiciones del contrato.

## Contratos

| Contrato | Objetivo y condiciones |
| --- | --- |
| Archivo fantasma | Recuperar el archivo principal y extraerse. La fuerza letal tiene una penalización elevada. |
| Relevo silencioso | Recuperar el archivo, sabotear el repetidor y extraerse. Una alarma confirmada cancela el contrato. |
| Doble filo | Recuperar el archivo y extraerse. Con varios agentes, uno recibe una orden de traición. La fuerza letal está permitida, con consecuencias para el sigilo. |

El archivo secundario y el repetidor pueden añadir recompensas u órdenes privadas. Cada operación combina módulos de obstáculos, variaciones de patrullas y una semilla nueva. Los corredores exteriores ofrecen accesos alternativos.

## Sigilo y entorno

Caminar, correr, agacharse y tumbarse cambian la velocidad, el ruido y la distancia a la que pueden verte. Pulsa **Z** para tumbarte: al introducir una dirección te arrastras automáticamente; al soltarla permaneces tumbado. Pulsa Z otra vez para levantarte. Los muebles sólidos bloquean el movimiento según su huella en el suelo. La cobertura baja protege a quien está agachado o tumbado; la alta interrumpe la visión. **R** permite adherirse a una cobertura alta y deslizarse por ella; mantén **Alt + dirección lateral** cerca de un extremo para asomarte y suelta para volver.

La niebla de guerra tiene una transición continua de opacidad que respeta paredes y aberturas, independiente de los tiles del suelo. Oculta enemigos, objetos e indicadores fuera de tu campo de visión o detrás de una superficie opaca. No guarda memoria de zonas exploradas. El alcance visual del agente se ha reducido un 12,5 % respecto a la referencia anterior, conservando las diferencias entre posturas y la orientación real; el zoom solo cambia la presentación. Los personajes utilizan sprites 2D de pixel art con fotogramas de movimiento.

La niebla permite ver las ondas de los ruidos que tú produces, aunque su origen quede fuera de tu visión. Otros agentes reciben las señales que pueden oír según la distancia y los obstáculos. Las señales ajenas ocultas indican una ubicación aproximada y desaparecen; no identifican a los enemigos que las escucharon ni revelan sus cuerpos. Los pasos de una patrulla cercana también pueden producir pistas breves y aproximadas en el suelo.

Los guardias distinguen patrulla, sospecha, investigación, alerta y búsqueda. Sus conos de observación, los obstáculos y tu postura afectan a la detección. Un ruido los atrae a su origen; no les revela automáticamente dónde estás. Después de perderte buscan en la última posición que pudieron observar. Si no consiguen avanzar, recalculan su ruta, prueban un punto próximo y pueden abandonar temporalmente un destino inaccesible, sin saltar a través de las esquinas.

El ojo táctico del HUD se ilumina por segmentos según el progreso real de detección y cambia al confirmarse. Sobre los guardias que puedes ver aparece el ojo progresivo y, cuando corresponde, una exclamación: sus estados de sospecha, persecución e inconsciencia conservan indicadores distintos. Los iconos no revelan guardias ocultos. Una alarma confirmada solicita refuerzos por accesos definidos: hasta tres oleadas de dos guardias y seis refuerzos activos. Tras 14 segundos sin nuevas pistas recuperan la patrulla; después de otros 12 segundos los refuerzos se retiran hacia sus salidas. No aparecen encima de un agente ni desaparecen mientras son visibles. Relevo silencioso conserva su derrota inmediata por alarma.

Mantén **G** para preparar un señuelo: la guía muestra su recorrido y destino, hasta 340 unidades, limitado por obstáculos y bordes. Suelta G para lanzarlo; clic derecho o Escape cancela. Pulsa **Q** mirando hacia una pared cercana para golpearla y atraer a los guardias que puedan oírla. El repetidor necesita agentes próximos: en cooperación se opera más rápido; también puede completarse individualmente.

Los botes de basura y armarios compatibles admiten un único ocupante: agente o cuerpo neutralizado. **E** entra y sale por un acceso libre; dentro no puedes moverte ni disparar. El armario solo se utiliza por su puerta frontal; dentro, **T** abre o cierra la abertura y permite observar un cono estrecho. Las mesas señaladas permiten ocultarte tumbado, mirar en la dirección del cursor y no almacenan cuerpos. Al salir de debajo de una mesa, el agente utiliza su dirección actual de visión; si ese lado está bloqueado, permanece oculto. Un guardia que confirmó tu presencia y vio la entrada puede acercarse y levantar la mesa durante una inspección de 1,5 segundos. Una patrulla que no vio la entrada no conoce automáticamente tu escondite; tampoco descubre un cuerpo almacenado simplemente por pasar junto al contenedor.

## Cámaras, trampas y puzles

Las cámaras barren entre límites angulares, hacen una pausa en los extremos y detectan según su orientación real, distancia, campo visual, postura del agente y obstáculos. El ojo avanza mientras permaneces expuesto; en una misión, confirmar la detección activa la alarma y la respuesta de seguridad. **T**, con una herramienta disponible junto a una cámara, la avería durante el resto de la operación. Un panel de control resuelto mediante puzle desactiva su grupo de cámaras durante **45 segundos**; después vuelven a vigilar. La central de seguridad también admite sabotaje con herramienta.

Los explosivos ocultos emiten un tic audible al aproximarte. La presencia del sonido depende de la distancia y las paredes; el dispositivo no aparece revelado desde cualquier lugar del escenario. Al entrar en su radio de activación comienza una cuenta de **5 segundos en misión**, u **8 segundos en entrenamiento**. La explosión causa daño según distancia y obstáculos y produce ruido; en misión también activa la respuesta de seguridad. Aléjate o desactiva el dispositivo antes de que termine la cuenta.

Pulsa **E** cerca de un explosivo, un computador, una cerradura protegida o un panel de cámaras para abrir su puzle. Cada etapa muestra dos señales: súmalas, calcula el resto de dividir entre cuatro y selecciona **resto + 1** entre los botones de la interfaz. Por ejemplo, 3 + 4 da resto 3, así que la respuesta es 4. El número de etapas depende del dispositivo, la misión y su dificultad.

Completar todas las etapas desactiva el explosivo, desbloquea y abre la puerta, descifra el computador o desactiva el grupo de cámaras. El computador aporta un objetivo secundario e información propia del contrato: rutas, ubicaciones de objetivos o datos de vigilancia. Una respuesta incorrecta en un explosivo provoca su detonación; en los demás dispositivos bloquea temporalmente otro intento y produce ruido. **Escape** o «Cancelar sin resolver» cierra la interfaz sin conceder el éxito. La operación continúa durante el puzle, incluso en solitario: las patrullas y los temporizadores siguen activos.

## CQC y armas

Pulsa **F brevemente** cerca de un guardia para golpear: las pulsaciones sucesivas alternan puñetazo y patada, con preparación, un único impacto, reacción del objetivo y recuperación. El alcance, la orientación y los obstáculos se comprueban al impactar. Los golpes acumulan aturdimiento sin causar daño letal; mantener F o la repetición automática del teclado no encadenan golpes.

Acércate **por detrás**, mirando en la misma dirección que el objetivo, y mantén **F** para agarrarlo. Debe estar a distancia válida, con la espalda accesible y sin alerta confirmada. La pareja conserva su posición física y el guardia deja de disparar. Suelta brevemente y vuelve a pulsar F tres veces para completar las fases de asfixia hasta dejarlo inconsciente. Mantener la tecla no completa el proceso. Soltarla durante más de **0,9 segundos** libera al objetivo; el daño y las acciones incompatibles también interrumpen la sujeción.

También puedes agarrar por detrás a un compañero y someterlo con los mismos controles. En misión queda incapacitado, vivo y sin perder salud; puede ser reanimado y no cuenta como eliminación ni como CQC enemigo para la puntuación. En entrenamiento sigue operativo gracias a la protección de la sala. No puedes golpear accidentalmente a un aliado mediante las pulsaciones breves. **V** empuja a un guardia, una diana de entrenamiento o un compañero con un desplazamiento corto, colisiones y enfriamiento, sin causar daño letal.

Apunta con el cursor táctico a cabeza, torso o extremidades: un impacto letal en la cabeza de un guardia elimina inmediatamente y uno no letal deja inconsciente. Los impactos en otras zonas acumulan daño o aturdimiento. Cada disparo consume munición y queda registrado aunque no impacte; las superficies sólidas detienen su trayectoria según su altura.

**1** selecciona la pistola y **2** el arma no letal. El **clic izquierdo** dispara el arma seleccionada; el **clic derecho** mantiene el acceso directo al disparo no letal. Cada arma tiene un cargador de **cuatro cartuchos**. Pulsa **B** para recargar: la maniobra tarda **1,2 segundos** y bloquea el disparo hasta finalizar. El HUD muestra cargador y munición total disponible, que incluye los cartuchos cargados. Cambiar de arma, recibir daño o iniciar una acción incompatible cancela la recarga. En misión el total es finito; el campo de entrenamiento permite repetir indefinidamente.

Los guardias que te detectan pueden disparar cuando tienen línea de visión y atacar cuerpo a cuerpo a corta distancia. Estar tumbado evita el daño directo de sus balas; sus ataques cuerpo a cuerpo siguen siendo peligrosos. Su munición es finita; al agotarla intentan acercarse para atacar. Los iconos distinguen sospecha, detección confirmada, combate e inconsciencia cuando puedes ver al guardia. Correr y ejecutar CQC consumen resistencia. Los ataques de los guardias que agotan tu salud o resistencia te dejan inconsciente y dejan de atacarte. En solitario, o si todo el equipo queda fuera de combate, aparece la derrota de la operación. Con otro compañero operativo, la sesión continúa y permite reanimación o recuperación temporal. La fuerza letal de otro agente puede eliminarte en Doble filo; incapacitar no satisface la orden de traición de eliminar al equipo.

Mantén **E** cerca de un guardia neutralizado para arrastrar un solo cuerpo y suelta para dejarlo. Acércalo a un contenedor vacío compatible y pulsa E para almacenarlo. Las paredes y los muebles bloquean tanto al agente como al cuerpo. Las patrullas reaccionan a los cuerpos expuestos que encuentran. En Doble filo un agente incapacitado puede ser reanimado por un compañero con E; incapacitar no satisface la orden de eliminar.

## Órdenes privadas y traición

**Tab** muestra únicamente tu orden privada: obtener una copia adicional, recuperar un dispositivo o cumplir la orden de traición de Doble filo. Las órdenes de otros agentes no se envían a tu cliente.

El traidor gana si elimina a todos los demás agentes sin activar alarmas. El equipo gana al cumplir el contrato y extraerse. El traidor comparte las reglas físicas y de percepción: sus disparos, ruidos y cuerpos pueden comprometer su propia operación. Las otras modalidades no asignan traidor.

## Informe y rango Maestro

Los rangos son **D, C, B, A, S, S+ y S++ — Maestro**. El registro de la operación conserva objetivos, extracción, detecciones, alarmas, disparos, CQC y cuerpos encontrados. La evaluación no depende únicamente del tiempo.

Maestro exige completar los objetivos obligatorios, extraerse y terminar sin armas letales o no letales, CQC contra enemigos, detecciones visuales confirmadas ni alarmas por descubrimiento del agente. Una detección invalida Maestro aunque después escapes. Las distracciones y las investigaciones de ruido sin descubrir al agente están permitidas. La incapacitación amistosa no concede recompensas de eliminación enemiga.

## Controles

| Acción | Control |
| --- | --- |
| Moverse | WASD o flechas |
| Correr | Mayús |
| Agacharse / levantarse | C |
| Tumbarse / levantarse | Z |
| Arrastrarse tumbado | Moverse tras tumbarse con Z |
| Interactuar / extraerse / reanimar | E |
| Golpear con puñetazo y patada alternados | Pulsar brevemente F |
| Agarrar por detrás a un guardia, diana o compañero | Mantener F |
| Asfixiar durante el agarre | Soltar brevemente y volver a pulsar F tres veces |
| Empujar guardia o compañero | V |
| Apuntar | Cursor táctico |
| Acercar / alejar la vista | Rueda del ratón |
| Seleccionar pistola / arma no letal | 1 / 2 |
| Disparar el arma seleccionada | Clic izquierdo |
| Disparo no letal directo | Clic derecho |
| Recargar el arma seleccionada | B |
| Preparar / lanzar señuelo | Mantener / soltar G |
| Cancelar preparación del señuelo | Clic derecho o Escape |
| Golpear pared cercana | Q |
| Arrastrar / soltar cuerpo o diana de prácticas | Mantener / soltar E |
| Entrar / salir de un escondite o almacenar cuerpo | E contextual |
| Adherirse / salir de cobertura alta | R |
| Abrir / cerrar la puerta desde dentro del armario | T |
| Asomarse desde un extremo de cobertura | Alt + dirección lateral |
| Herramienta contextual | T |
| Abrir puzle de dispositivo | E contextual |
| Responder al puzle | Botones 1–4 de la interfaz |
| Cancelar puzle sin resolver | Escape o botón de cancelación |
| Rearmar dispositivos y dianas de entrenamiento | E junto a REARMAR PRUEBAS |
| Mostrar orden privada | Tab |
| Sala de despliegue en la base | F2 |
| Pausa local | Escape |
| Reanudar, reiniciar, opciones, base, menú o salir | Menú propio de pausa |
| Captura | F12 |
| Alternar pantalla completa | F11 o Alt + Enter |

En una partida individual la pausa detiene la simulación. En LAN el mundo continúa para los demás; tu agente deja de recibir órdenes de movimiento mientras tienes la pausa abierta o la ventana sin foco. El anfitrión controla el reinicio de una operación y el regreso del equipo a la base. El zoom solo cambia tu vista y conserva la puntería sobre el mundo.

## Pantalla y sonido

El menú conserva el logotipo, el icono y el cursor propios. La portada estática muestra dos agentes y utiliza una paleta de 16 colores. «Opciones» permite seleccionar resoluciones reales, ventana o pantalla completa, volumen y velocidad del cursor con mando. Los cambios de pantalla requieren confirmación; si no los conservas se revierte la configuración anterior. Los ajustes se guardan localmente.

Los efectos utilizan **26 archivos WAV originales**: pistola, rifle, disparo discreto, impactos, CQC, caídas, pasos en suelo duro o blando, golpes en paredes, temporizador y explosión, cerraduras, circuitos, cámaras, alarmas, refuerzos, armarios, escondites, arrastre y señuelos. Los disparos utilizan transitorios y ruido de percusión, sin el barrido tonal anterior. La señal de recarga acompaña tanto el reequipamiento en armería como la recarga manual con B. Los efectos se activan por eventos reales y se atenúan según distancia y obstáculos.

## Jugar por LAN

1. Usa la misma versión del juego en los equipos y conéctalos a la misma red local.
2. El anfitrión elige **Crear sala LAN**. La sala muestra su IP y puerto; el predeterminado es **55031/TCP**.
3. Los demás eligen **Conectar por IP** e introducen esos datos.
4. Permite el tráfico del juego en la red privada cuando Windows lo solicite.
5. Prepara el equipo, marca cada agente como preparado y despliega desde el anfitrión.

El protocolo de esta versión es **5**: el anfitrión resuelve vigilancia, explosivos, puzles, armas, CQC, protección de entrenamiento, navegación y alarmas, y cada cliente recibe los resultados y las señales que puede percibir. Usa la misma versión en todos los equipos. La implementación tiene pruebas automáticas de transporte y sesión; los resultados de cada entrega se documentan localmente. **La validación en dos computadores físicos sigue pendiente**: las pruebas locales entre procesos no la sustituyen.

## Ejecutar

Desde la raíz del proyecto, con las herramientas C++ configuradas:

```powershell
cmd /c .vscode\build.bat --parallel 2
.\build\AwesomeEngine.exe
```

El motor conserva Bounce y Clicker para desarrollo mediante `--scene Bounce` y `--scene Clicker`; no aparecen como opciones del menú de NO TRACE. La documentación técnica y los registros de pruebas se conservan localmente en `docs/`.
