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

La base y sala de espera es un escenario recorrible de **1900 × 1060**: las estaciones de preparación están al oeste y el campo de prácticas al este. El mapa completo permanece visible, sin niebla de guerra. Puedes moverte, usar posturas, coberturas y escondites, probar las armas y CQC, lanzar señuelos, generar ruido y resolver los dispositivos. Los obstáculos conservan sus colisiones. No aparecen guardias enemigos, patrullas ni refuerzos en esta zona.

El campo tiene **tres dianas** con aspecto propio, indicador de salud y contador de impactos. Reciben disparos, golpes, agarres, sometimientos, explosiones y empujones; su salud visible se detiene en **1** y nunca mueren, quedan inconscientes ni desaparecen por daño. Mantén **E** para practicar su arrastre aun estando activas; suelta para dejarlas. Las dianas no se guardan dentro de contenedores.

Los jugadores también permanecen vivos y operativos ante disparos de compañeros, explosiones y sometimientos. La salud se recupera automáticamente a **30 puntos por segundo**, hasta el máximo. El anfitrión aplica la recuperación y la protección al resolver el daño y las rutas de incapacitación, también en LAN. Las cámaras muestran su progreso de detección, pero durante las prácticas no activan una alarma de misión ni solicitan guardias.

Puedes practicar con una cámara, su panel, un explosivo, una cerradura, un computador con palancas distribuidas, un relé de coordinación, un armario, un contenedor y una mesa. El explosivo de prácticas tiene **8 segundos** de cuenta. Acércate a la consola **REARMAR PRUEBAS** situada al inicio del campo y pulsa E para restaurar dispositivos, cámaras, silenciadores y dianas y repetir los ejercicios. Los escondites ocupados conservan sus agentes; una puerta ocupada permanece abierta al rearmar.

En entrenamiento se reponen munición, señuelos y herramientas. Con la configuración de munición ilimitada de prácticas, el total de cada tipo vuelve a **48**, pero sus cargadores de cuatro cartuchos se vacían y exigen una recarga real. El equipo elegido en armería se guarda por separado: practicar con armas no añade munición a una misión preparada sin ellas. Al desplegar regresan los guardias, el daño normal, las alarmas y las condiciones del contrato.

## Contratos

| Contrato | Objetivo y condiciones |
| --- | --- |
| Archivo fantasma | Recuperar el archivo principal y extraerse. La fuerza letal tiene una penalización elevada. |
| Relevo silencioso | Recuperar el archivo, sabotear el repetidor y extraerse. Una alarma confirmada cancela el contrato. |
| Doble filo | Recuperar el archivo y extraerse. Con varios agentes, uno recibe una orden de traición. La fuerza letal está permitida, con consecuencias para el sigilo. |

El archivo secundario es una misión secundaria necesaria para S++ en los tres contratos. El repetidor es obligatorio en Relevo silencioso y puede aportar recompensas u órdenes privadas en los demás contratos; las recompensas de otros dispositivos no sustituyen una misión secundaria pendiente. Cada operación combina módulos de obstáculos, variaciones de patrullas y una semilla nueva. Los corredores exteriores ofrecen accesos alternativos.

## Sigilo y entorno

Caminar, correr, agacharse y tumbarse cambian la velocidad, el ruido y la distancia a la que pueden verte. Pulsa **Z** para tumbarte: al introducir una dirección te arrastras automáticamente; al soltarla permaneces tumbado. Pulsa Z otra vez para levantarte. Los muebles sólidos bloquean el movimiento según su huella en el suelo. La cobertura baja protege a quien está agachado o tumbado; la alta interrumpe la visión. **Alt** permite adherirse a una cobertura alta, con la espalda hacia la pared, y deslizarse por ella. Al llegar a un extremo se produce el asomado automáticamente. La cabeza y el torso expuestos pueden ser vistos; la pared protege únicamente donde su geometría corta la línea de visión. La puntería y el láser comparten esa posición física.

En misión, la niebla de guerra tiene una transición continua de opacidad que respeta paredes y aberturas, independiente de los tiles del suelo. Oculta enemigos, objetos e indicadores fuera de tu campo de visión o detrás de una superficie opaca. No guarda memoria de zonas exploradas. El alcance visual está configurado al **50 % de la referencia**: 200 unidades de pie, 165 agachado y 130 tumbado, conservando la orientación y la oclusión reales. El zoom va de **1,8 a 4,5**, con **2,0** por defecto: permite acercar más la cámara y limita el alejamiento, sin cambiar las distancias de la simulación. Los personajes utilizan sprites 2D de pixel art en ocho direcciones, con ciclos de locomoción, cambios de postura, combate, recarga y maniobras tácticas. El apuntado, el disparo y la recarga conservan la postura de pie, agachada o tumbada. Los uniformes distinguen al equipo y a los guardias.

La niebla permite ver las ondas de los ruidos que tú produces, aunque su origen quede fuera de tu visión. El radio al correr es de **306 unidades**, un 10 % menos que las 340 anteriores; la onda representa ese mismo alcance. Las patrullas oyen cada evento dentro de su radio propio aunque haya paredes, sin obtener una detección visual ni seguir automáticamente al emisor. Otros agentes reciben las señales que pueden oír según la distancia y los obstáculos. Las señales ajenas ocultas indican una ubicación aproximada y desaparecen; no identifican a los enemigos que las escucharon ni revelan sus cuerpos. Los pasos de una patrulla cercana también pueden producir pistas breves y aproximadas en el suelo.

Correr genera pasos rápidos y fuertes con un radio acústico de **306 unidades**; caminar, pasos normales de **70**; moverse agachado, pasos suaves de **12**, o **22** al avanzar deprisa agachado. Arrastrarse tumbado no genera pasos ni ruido de desplazamiento. Los disparos, explosiones y distracciones conservan su propio ruido en cualquier postura. Las ondas utilizan el mismo radio que escucha la IA.

Los guardias distinguen patrulla, sospecha, investigación, alerta y búsqueda. Al alcanzar un punto de patrulla se detienen entre **10 y 20 segundos** para vigilar antes de seguir. Las pistas y amenazas interrumpen la pausa. Sus conos de observación, los obstáculos y tu postura afectan a la detección. Un ruido los atrae a su origen; no les revela automáticamente dónde estás. Después de perderte buscan en la última posición que pudieron observar. Si no consiguen avanzar, recalculan su ruta, prueban un punto próximo y pueden abandonar temporalmente un destino inaccesible, sin saltar a través de las esquinas.

El ojo táctico junto al mapa representa la alerta global: roja, naranja, amarilla y calma. Su color no depende del progreso individual de un guardia. Sobre los guardias y cámaras que puedes ver aparece el ojo progresivo y, cuando corresponde, una exclamación: sospecha, detección confirmada, persecución y búsqueda conservan estados distintos. La sospecha permanece al menos **1,5 segundos** desde el último contacto; la confirmación, **5 segundos**, y se renuevan mientras existe percepción válida. Los iconos no revelan entidades ocultas ni permanecen indefinidamente tras perder el contacto. Una alarma confirmada solicita refuerzos por accesos definidos: hasta tres oleadas de dos guardias y seis refuerzos activos. Al perder el último contacto confirmado comienzan 20 segundos de búsqueda naranja y después 12 de vigilancia amarilla. Los refuerzos se retiran durante la amarilla; las patrullas normales recuperan su ruta al terminarla. No aparecen encima de un agente ni desaparecen mientras son visibles. Relevo silencioso conserva su derrota inmediata por alarma.

Mantén **G** para preparar un disco señuelo: la guía muestra su recorrido y destino, hasta 340 unidades, limitado por obstáculos y bordes. Suelta G para lanzarlo; clic derecho o Escape cancela la preparación. El disco recorre una trayectoria física durante el vuelo y genera el ruido de distracción al aterrizar. Pulsa **F** mientras estás adherido a una pared con Alt para golpearla: el ruido nace en el punto de contacto y atrae a quienes puedan oírlo. En ese contexto, F da prioridad al golpe de pared.

Los botes de basura y armarios compatibles admiten un único ocupante: agente o cuerpo neutralizado. **E** entra y sale por un acceso libre; dentro no puedes moverte ni disparar. El armario solo se utiliza por su puerta frontal; dentro, **T** abre o cierra la abertura y permite observar un cono estrecho. Las mesas señaladas permiten ocultarte tumbado y observar en **360 grados**, respetando el tablero, las paredes y los demás obstáculos; no almacenan cuerpos. La entrada y la salida bajo mesa son inmediatas y no reproducen animaciones. WASD fija la dirección de salida y el cursor conserva la orientación del arma sin sacar al personaje. Si la salida está bloqueada, permanece oculto. El tablero proporciona ocultación física, pero un guardia o una cámara con línea de visión válida aún puede detectarte. Un guardia que confirmó tu presencia y vio la entrada puede acercarse y levantar la mesa durante una inspección de 1,5 segundos. Un cuerpo almacenado sigue oculto a las patrullas que pasan junto al contenedor.

## Cámaras, trampas y puzles

Las cámaras barren entre límites angulares, hacen una pausa en los extremos y detectan según su orientación real, distancia, campo visual, postura del agente y obstáculos. Cuando adquieren un objetivo, lo siguen físicamente y lo conservan mientras puedan verlo, aunque otro agente pase más cerca. Al perderlo buscan durante **3 segundos** en la última posición observada. Los tiempos base de confirmación son **1,5 segundos** en cámaras y **2,4** en guardias; la distancia, postura y movimiento modifican la acumulación. Correr facilita la detección y estar agachado o tumbado la reduce. Una oclusión breve conserva el progreso antes de empezar a disiparlo; en misión, esto activa la alarma y la respuesta de seguridad. El ojo conserva los temporizadores de sospecha y confirmación, y se apaga al desactivar la cámara. **T**, con una herramienta disponible junto a una cámara, la avería durante el resto de la operación. Un panel de control resuelto mediante puzle desactiva su grupo de cámaras durante **45 segundos**; después vuelven a vigilar. La central de seguridad también admite sabotaje con herramienta.

Los explosivos ocultos emiten un tic audible al aproximarte. La presencia del sonido depende de la distancia y las paredes; el dispositivo no aparece revelado desde cualquier lugar del escenario. Al entrar en su radio de activación comienza una cuenta de **5 segundos en misión**, u **8 segundos en entrenamiento**. La explosión causa daño según distancia y obstáculos y produce ruido; en misión también activa la respuesta de seguridad. Aléjate o desactiva el dispositivo antes de que termine la cuenta.

Pulsa **E** cerca de un dispositivo para consultar o iniciar su intervención. Hay cuatro familias reutilizables:

- **Flechas:** reproduce la secuencia de direcciones mostrada por el circuito. Las cerraduras se abren al completar el orden correcto.
- **Cables:** elige un origen y un puerto del mismo color y letra. Cada extremo se utiliza una vez y se respetan las reglas de cruces del dispositivo. Resolver el panel modifica su grupo real de cámaras; resolver el explosivo aísla su detonador.
- **Coordinación:** dos agentes mantienen sus controles simultáneamente durante **2 segundos**. Soltar, cancelar o perder una condición necesaria reinicia el progreso. Si hay un único jugador conectado, el relé auxiliar permite completar la prueba individualmente; un compañero incapacitado no reduce el requisito de la partida cooperativa.
- **Palancas distribuidas:** el computador muestra un orden de cuatro palancas cercanas. Cierra el panel, desplázate y acciona cada palanca con E. Sus estados cambian en el escenario y la secuencia correcta permite extraer la inteligencia del computador.

El computador aporta una recompensa secundaria e información propia del contrato: rutas, ubicaciones de objetivos o datos de vigilancia. Una respuesta incorrecta en un explosivo provoca su detonación; en los demás dispositivos genera un aviso sonoro local, reinicia el circuito y bloquea el reintento durante **3 segundos**. **Escape** o «Cancelar sin resolver» cierra la interfaz sin conceder el éxito. El anfitrión valida las entradas, la distancia y los usuarios. La operación continúa durante el puzle, incluso en solitario: las patrullas y los temporizadores siguen activos.

## CQC y armas

Pulsa **F brevemente** cerca de un guardia para golpear: las pulsaciones sucesivas alternan puñetazo y patada, con preparación, un único impacto, reacción del objetivo y recuperación. El alcance, la orientación y los obstáculos se comprueban al impactar. Los golpes acumulan aturdimiento sin causar daño letal; mantener F o la repetición automática del teclado no encadenan golpes.

Acércate a un objetivo válido, orienta al agente hacia él y mantén **F** para agarrarlo. Dentro del alcance CQC, el agente puede aproximarse físicamente a su espalda si existe un recorrido libre; las paredes, la separación y el tiempo de aproximación pueden interrumpir el intento. El agarre solo se establece con una posición posterior válida. La pareja conserva su posición física y el guardia deja de disparar. Suelta brevemente y vuelve a pulsar F tres veces para completar las fases de asfixia hasta dejarlo inconsciente. Mantener la tecla no completa el proceso. El inicio alcanza objetivos a **76 unidades**, pero el agarre se completa a distancia física de sujeción después de una aproximación con colisiones. Soltar F conserva al objetivo sujeto. Mantener una presión continua durante **2,25 segundos** una vez establecido el agarre provoca su liberación; las pulsaciones breves separadas correctamente reinician esa presión y permiten completar el sometimiento. El daño, una separación imposible o las acciones incompatibles también interrumpen la sujeción.

También puedes agarrar por detrás a un compañero y someterlo con los mismos controles. En misión queda incapacitado, vivo y sin perder salud; puede ser reanimado y no cuenta como eliminación ni como CQC enemigo para la puntuación. En entrenamiento sigue operativo gracias a la protección de la sala. No puedes golpear accidentalmente a un aliado mediante las pulsaciones breves. **V** empuja a un guardia, una diana de entrenamiento o un compañero con un desplazamiento corto, colisiones y enfriamiento, sin causar daño letal.

Apunta con el cursor táctico a cabeza, torso o extremidades: un impacto letal en la cabeza de un guardia elimina inmediatamente y uno no letal deja inconsciente. Los impactos en otras zonas acumulan daño o aturdimiento. Cada disparo consume munición y queda registrado aunque no impacte; las superficies sólidas detienen su trayectoria según su altura.

Mantén **Q** para mostrar las siluetas de ambas armas, resaltar la seleccionada y consultar únicamente sus cartuchos cargados y capacidad, por ejemplo **4 / 4**. Suelta Q para ocultarlo inmediatamente. También puedes usar **1** para la pistola y **2** para el arma no letal. Mantén **clic derecho** para apuntar y suéltalo para volver al movimiento normal; el **clic izquierdo** dispara el arma seleccionada. El equipo estándar incluye **24 cartuchos de pistola y 24 no letales**: cada arma tiene un cargador de **cuatro cartuchos** y comienza con **20 de reserva**. Los perfiles de infiltración, no letal y sabotaje conservan sus cantidades propias. Pulsa **R** para recargar: tarda **1,2 segundos** y bloquea el disparo hasta finalizar. El HUD separa cartuchos cargados, capacidad y reserva, por ejemplo **4 / 4 + 20**; el total preparado es cargador más reserva. Cambiar de arma, recibir daño o iniciar una acción incompatible cancela la recarga. En misión el total es finito; el campo de entrenamiento permite repetir indefinidamente.

Cada arma conserva un silenciador con durabilidad propia. **H** lo monta o desmonta; los disparos con él instalado reducen su estado hasta romperlo. El arma sigue funcionando al agotarlo, pero los disparos siguientes producen más ruido y se oyen a mayor distancia. El HUD muestra su estado. Instalarlo y retirarlo producen sonidos mecánicos distintos, una sola vez por cambio efectivo. Cambiar de arma o recargar conserva el desgaste; rearmar las prácticas o desplegar una nueva operación restaura el equipo.

Mientras ya corres de pie, pulsa **Espacio** para ejecutar una zambullida hacia delante y terminar tumbado. Agachado, pulsa **Ctrl izquierdo** para rodar en la dirección de movimiento o de visión y terminar agachado. Ambas maniobras consumen resistencia, generan ruido y tienen recuperación y enfriamiento. Las colisiones detienen el desplazamiento. No se combinan con disparos, recarga, CQC, arrastre, cobertura, escondites ni puzles. Sus teclas pueden cambiarse desde **Opciones → Controles y teclas asignadas**, eligiendo dos asignaciones distintas.

Los guardias que te detectan pueden disparar cuando tienen línea de visión y atacar cuerpo a cuerpo a corta distancia. Estar tumbado evita el daño directo de sus balas; sus ataques cuerpo a cuerpo siguen siendo peligrosos. Su munición es finita; al agotarla intentan acercarse para atacar. Los iconos distinguen sospecha, detección confirmada, combate e inconsciencia cuando puedes ver al guardia. La cabeza inclinada con **Zzz** identifica a una persona inconsciente; la calavera distingue un cadáver. Ambos indicadores respetan la visibilidad y mantienen su tamaño en pantalla al cambiar el zoom.

Al ver un cuerpo, un guardia se acerca físicamente, lo inspecciona durante **1,5 segundos** y comunica el hallazgo sin conocer la posición del jugador. Si el compañero está inconsciente y tiene vida, lo despierta sin moverlo de lugar ni reiniciar la búsqueda naranja; los cadáveres no se reaniman. Un solo guardia presta asistencia y la interrumpe si aparece una amenaza. La alerta **roja** permanece mientras exista contacto confirmado. Al perderlo comienza una búsqueda **naranja de 20 segundos**, con rutas distribuidas alrededor de la última pista. Después hay **12 segundos de vigilancia amarilla**: los refuerzos comienzan a retirarse y desaparecen al alcanzar una salida válida fuera de la vista del jugador. Los guardias normales vuelven a patrullar al terminar la vigilancia. Encontrar de nuevo el mismo cuerpo no reinicia indefinidamente la alerta. El anfitrión controla los estados y los tiempos en LAN. El ojo junto al mapa muestra directamente la alerta global: roja, naranja, amarilla y normal al recuperar la calma, sin confundirse con la detección individual de cada guardia.

Correr y ejecutar CQC consumen resistencia. La vida máxima es **200** y permite resistir varios impactos corporales normales; los disparos a la cabeza siguen siendo más peligrosos. Tras **12 segundos consecutivos sin daño**, un agente operativo recupera lentamente **1 punto de vida por segundo**, hasta el máximo; un nuevo impacto reinicia la espera. Esta recuperación nunca revive a muertos o incapacitados. Agotar la vida en solitario confirma la muerte y la derrota. Con otro compañero del equipo operativo, el agente queda incapacitado durante un plazo autoritativo de **55 segundos**. El HUD muestra la cuenta y una flecha de orientación hacia el compañero, incluso fuera de pantalla, sin revelar enemigos ni datos de su entorno. Un aliado debe llegar y pulsar **E** a menos de 48 unidades y sin una pared entre ambos para recuperarlo con **70 puntos de vida**. Si vence el plazo o todos quedan fuera de combate, la operación termina en derrota. Agotar resistencia por combate también puede incapacitar, pero la resistencia y la espera nunca reaniman automáticamente en misión. La fuerza letal de otro agente puede eliminarte en Doble filo; incapacitar no satisface la orden de traición de eliminar al equipo.

Mantén **E** cerca de un guardia neutralizado para arrastrar un solo cuerpo y suelta para dejarlo. Acércalo a un contenedor vacío compatible y pulsa E para almacenarlo. Las paredes y los muebles bloquean tanto al agente como al cuerpo. Las patrullas reaccionan a los cuerpos expuestos que encuentran. En Doble filo un agente incapacitado puede ser reanimado por un compañero con E; incapacitar no satisface la orden de eliminar.

## Órdenes privadas y traición

**Tab** muestra únicamente tu orden privada: obtener una copia adicional, recuperar un dispositivo o cumplir la orden de traición de Doble filo. Las órdenes de otros agentes no se envían a tu cliente.

El traidor gana si elimina a todos los demás agentes sin activar alarmas. El equipo gana al cumplir el contrato y extraerse. El traidor comparte las reglas físicas y de percepción: sus disparos, ruidos y cuerpos pueden comprometer su propia operación. Las otras modalidades no asignan traidor.

## Informe y rango Maestro

Los rangos son **D, C, B, A, S, S+ y S++ — Maestro**. El registro de la operación conserva objetivos, extracción, detecciones, alarmas, disparos, CQC y cuerpos encontrados. La evaluación no depende únicamente del tiempo.

Maestro exige completar los objetivos obligatorios y **todas las misiones secundarias declaradas por el contrato**, extraerse y terminar sin armas letales o no letales, CQC contra enemigos, detecciones visuales confirmadas ni alarmas por descubrimiento del agente. Una detección invalida Maestro aunque después escapes. Las distracciones y las investigaciones de ruido sin descubrir al agente están permitidas. La incapacitación amistosa no concede recompensas de eliminación enemiga. El informe muestra las condiciones que impidieron S++ y conserva el motivo de la última operación. Recoger un objeto opcional o resolver otro dispositivo no completa una misión secundaria distinta.

## Controles

WASD controla el movimiento axonométrico y la orientación del cuerpo en ocho direcciones. Al detenerte conservas la última orientación; mover el ratón no gira el cuerpo. Mantener clic derecho orienta el arma hacia el cursor y activa la mira y el láser desde el cañón. El disparo sigue esa misma dirección. En cobertura, la normal de la pared fija la postura, incluso al apuntar o deslizarte; F golpea la pared con el brazo sin girar el cuerpo. Las maniobras y el CQC conservan su orientación durante la acción.

| Acción | Control |
| --- | --- |
| Moverse | WASD o flechas |
| Correr | Mayús |
| Agacharse / levantarse | C |
| Tumbarse / levantarse | Z |
| Arrastrarse tumbado | Moverse tras tumbarse con Z |
| Interactuar / extraerse / reanimar | E |
| Golpear con puñetazo y patada alternados | Pulsar brevemente F |
| Aproximarse y agarrar por detrás a un guardia, diana o compañero | Mantener F |
| Asfixiar durante el agarre | Soltar brevemente y volver a pulsar F tres veces |
| Empujar guardia o compañero | V |
| Apuntar con el arma | Mantener clic derecho y orientar el cursor |
| Acercar / alejar la vista | Rueda del ratón |
| Seleccionar pistola / arma no letal | 1 / 2 |
| Mostrar / ocultar selector de armas | Mantener / soltar Q |
| Disparar el arma seleccionada | Clic izquierdo |
| Recargar el arma seleccionada | R |
| Montar / desmontar silenciador | H |
| Preparar / lanzar señuelo | Mantener / soltar G |
| Cancelar preparación del señuelo | Clic derecho o Escape |
| Golpear pared en cobertura | F contextual |
| Arrastrar / soltar cuerpo o diana de prácticas | Mantener / soltar E |
| Entrar / salir de un escondite o almacenar cuerpo | E contextual |
| Adherirse / salir de cobertura alta | Alt |
| Abrir / cerrar la puerta desde dentro del armario | T |
| Asomarse desde un extremo de cobertura | Automático al aproximarse al borde |
| Zambullirse mientras corres de pie | Espacio, configurable |
| Rodar estando agachado | Ctrl izquierdo, configurable |
| Herramienta contextual | T |
| Abrir puzle de dispositivo | E contextual |
| Operar un puzle | Flechas, pares de cables o control mantenido en la interfaz; E en las palancas físicas |
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

El menú conserva el logotipo, el icono y el cursor propios. La portada estática muestra dos agentes y utiliza una paleta de 16 colores. La ventana aparece centrada en el área útil del monitor principal y vuelve a centrarse al cambiar su resolución. Pantalla completa utiliza la resolución del escritorio; también hay ventana sin bordes.

«Opciones», disponible en el menú y en la pausa, permite ajustar resolución de ventana, VSync, límite de FPS, volumen general y de efectos, velocidad del cursor con mando y las teclas de zambullida y rodada. Incluye restauración de vídeo, audio, mando y controles. Los cambios de pantalla requieren confirmación; si no los conservas se revierte la configuración anterior. Los ajustes se guardan localmente y se restauran al iniciar.

Los efectos utilizan **28 archivos WAV originales**: pistola, rifle, disparo discreto, impactos, CQC, caídas, pasos en suelo duro o blando, golpes en paredes, temporizador y explosión, cerraduras, circuitos, cámaras, alarmas, refuerzos, armarios, escondites, arrastre y señuelos. Los disparos utilizan transitorios y ruido de percusión, sin el barrido tonal anterior. La señal de recarga acompaña tanto el reequipamiento en armería como la recarga manual con R. Los efectos se activan por eventos reales y se atenúan según distancia y obstáculos.

## Jugar por LAN

1. Usa la misma versión del juego en los equipos y conéctalos a la misma red local.
2. El anfitrión elige **Crear sala LAN**. La sala muestra su IP y puerto; el predeterminado es **55031/TCP**.
3. Los demás eligen **Conectar por IP** e introducen esos datos.
4. Permite el tráfico del juego en la red privada cuando Windows lo solicite.
5. Prepara el equipo, marca cada agente como preparado y despliega desde el anfitrión.

El protocolo de esta versión es **7**: el anfitrión resuelve vigilancia, explosivos, puzles, armas, silenciadores, CQC, maniobras, protección y recuperación de entrenamiento, navegación y alarmas. Cada cliente recibe los resultados y las señales que puede percibir, conservando la privacidad de órdenes y desafíos ajenos. Usa la misma versión en todos los equipos. La implementación tiene pruebas automáticas de transporte y sesión; los resultados de cada entrega se documentan localmente. **La validación en dos computadores físicos sigue pendiente**: las pruebas locales entre procesos no la sustituyen.

## Ejecutar

Desde la raíz del proyecto, con las herramientas C++ configuradas:

```powershell
cmd /c .vscode\build.bat --parallel 2
.\play.exe
```

El build termina desarrollo y después prepara una versión Release independiente en `production/`. `play.exe` abre la última publicación válida; puedes seguir jugando con ella durante otra compilación. Si un build falla, se conserva la publicación anterior. Las versiones de producción comparten su guardado en `production/user-data/Save.json`, separado de la partida de desarrollo.

Para depurar directamente se mantiene `.\build\AwesomeEngine.exe` desde la raíz. Las compilaciones y publicaciones generan sus registros en `production/logs/`.

El motor conserva Bounce y Clicker para desarrollo mediante `--scene Bounce` y `--scene Clicker`; no aparecen como opciones del menú de NO TRACE. La documentación técnica y los registros de pruebas se conservan localmente en `docs/`.

La prueba `NoTraceGameGui` genera una secuencia visual en `build/gui-test-sandbox/captures/world-clips-*.png`: ocho direcciones, fases de cada acción y tres niveles de zoom. Incluye locomoción, cambios de postura, armas de pie/agachado/tumbado, CQC, cobertura, escondites, zambullida y rodada. Puede regenerarse con `ctest --test-dir build -R NoTraceGameGui --output-on-failure`; utiliza una copia del contenido y verifica que el guardado original conserve su contenido.
