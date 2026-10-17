# NO TRACE — Desarrollado con Awesome Engine

**Autor:** Carlos Cortina.

**Organización ficticia de espionaje:** CIO — Clandestine Intelligence Organization.

NO TRACE es un proyecto de infiltración táctica en pixel art con cámara axonométrica, concebido a escala viable para el Caribe Game Jam. La presentación incorpora un menú con identidad propia, una recepción informativa de CIO y opciones de pantalla y volumen. Las mecánicas de infiltración descritas a continuación siguen siendo diseño previsto.

## Presentación disponible

Al abrir el proyecto aparece el logotipo de NO TRACE sobre un fondo estático de una instalación nocturna con un agente armado. «Nueva operación» lleva a la recepción de CIO, desde donde se puede volver al menú. «Opciones» permite elegir una resolución compatible, alternar ventana y pantalla completa, ajustar volumen y velocidad del cursor con mando, y restaurar los valores predeterminados. «Salir» cierra el juego.

Se puede usar el cursor táctico o recorrer los controles con Tab y activarlos con Enter o Espacio. En Opciones, las flechas ajustan el deslizador enfocado. F11 y Alt + Enter alternan el modo de pantalla. Los cambios de pantalla se confirman antes de guardarse; si no se conservan a tiempo, se restaura el modo anterior. Escape abre la pausa o revierte un cambio de pantalla pendiente; M regresa al menú desde las escenas disponibles.

La recepción presenta el acceso inicial a la organización. La base recorrible, el equipo y las misiones se desarrollarán en las siguientes fases.

## Identidad del juego

El género combina infiltración táctica, sigilo, cooperación, traición y elementos roguelike. La referencia de diseño es la libertad de acción y la infiltración de **Metal Gear Solid V: The Phantom Pain**. NO TRACE desarrollará su propia identidad, personajes, instalaciones y recursos originales.

Los agentes pertenecen a CIO, una organización ficticia dedicada a operaciones clandestinas. La discreción, la preparación y el criterio durante una operación definirán su desempeño.

## Concepto y objetivo

Los agentes deberán entrar en una instalación, cumplir un contrato y alcanzar la extracción. Cada contrato establecerá qué información recuperar, qué dispositivos intervenir u otros objetivos de la operación, junto con las condiciones para completarlos.

La propuesta combina varias maneras de resolver un problema con información que no siempre comparte todo el equipo. Una distracción puede abrir una ruta, un compañero puede facilitar un acceso y una orden secreta puede cambiar las prioridades de un agente. Se busca que las decisiones y sus consecuencias generen tensión, incluso cuando nadie dispara.

## Bucle de juego

1. Regresar o permanecer en la base de operaciones de CIO.
2. Seleccionar una misión y preparar el equipo.
3. Revisar los objetivos y las condiciones de la operación.
4. Infiltrarse y explorar la instalación.
5. Resolver los objetivos mediante diferentes estrategias.
6. Gestionar enemigos, alarmas, riesgos y objetivos secretos.
7. Extraerse y evaluar la misión.
8. Regresar a la base y preparar la siguiente operación.

## Sistema de movimiento y sigilo

El agente podrá caminar, correr, agacharse y desplazarse agachado, tumbarse y arrastrarse. **Tumbarse y arrastrarse son acciones distintas:** la primera cambia la postura; la segunda permite avanzar mientras se permanece tumbado. Cada postura tendrá efectos propios sobre velocidad, visibilidad y posibilidades de detección.

El diseño permitirá aprovechar coberturas bajas y altas y ocultarse en objetos o lugares que admitan esa acción. La forma del escenario determinará qué protege de la visión: no cualquier objeto servirá como cobertura o escondite. Puertas, dispositivos y otros elementos ofrecerán interacciones contextuales.

Correr permitirá cubrir distancia rápidamente, pero el ruido previsto aumentará el riesgo de atraer guardias. Elegir una postura y un recorrido será parte de la estrategia.

## Interacción con el entorno

Se prevé lanzar objetos y golpear paredes para crear distracciones acústicas. Los agentes podrán producir ruidos en posiciones elegidas para atraer guardias y alterar temporalmente sus patrullas.

La infiltración incluirá desactivar cámaras y alarmas cuando el contrato y las herramientas lo permitan, usar accesos alternativos e intervenir puertas, terminales y dispositivos. También se contempla esconder cuerpos para reducir el riesgo de que las patrullas los encuentren y colaborar con otros agentes para resolver obstáculos.

Estas posibilidades forman parte del diseño pendiente de desarrollo. Su utilidad dependerá de las condiciones de cada instalación y de los recursos disponibles.

## Enemigos y alarmas

Los guardias patrullarán rutas y reaccionarán a lo que vean y escuchen. Su visión considerará distancia, orientación, obstáculos, cobertura y postura del agente. La audición les permitirá investigar una fuente de ruido sin haber identificado a quien la produjo.

Los estados previstos incluyen patrulla, sospecha, investigación, alerta, persecución y búsqueda de la última posición conocida. Una **detección visual confirmada** activará la alarma según las reglas del contrato. Escuchar un ruido podrá causar sospecha o investigación; ese sonido por sí solo no confirmará la presencia de un agente.

La distribución de patrullas, las coberturas y las distracciones cambiará las oportunidades de infiltración. Tras romper la línea de visión, el agente deberá gestionar la búsqueda enemiga y encontrar otra ruta segura.

## Combate y equipamiento

El combate cuerpo a cuerpo o CQC permitirá agarrar, derribar e inmovilizar enemigos cuando exista una oportunidad de aproximación. El resultado de esas acciones deberá distinguirse con claridad:

- **Neutralizar:** impedir que un enemigo interfiera; describe el resultado táctico y no implica necesariamente su muerte.
- **Incapacitar:** dejar al enemigo temporalmente fuera de acción, con un estado diferenciado.
- **Eliminar:** causar su muerte de forma definitiva.

La preparación incluirá armas letales, armas no letales, dispositivos de distracción y herramientas de infiltración. El equipo será limitado: escoger qué llevar obligará a anticipar obstáculos y aceptar riesgos.

Algunos contratos podrán autorizar fuerza letal. Otros exigirán discreción o prohibirán determinados medios; disparos, cuerpos encontrados y acciones ruidosas tendrán consecuencias según esas condiciones.

## Puntuación y rangos

Los rangos previstos, de menor a mayor, son **D, C, B, A, S, S+ y S++**. La evaluación considerará el cumplimiento de objetivos principales y secundarios, las condiciones de extracción, el uso de armas, las detecciones y las alarmas. El contrato determinará qué acciones están autorizadas y cómo afectan al resultado. Cuando corresponda, se distinguirá la evaluación del equipo del desempeño individual.

**S++ — Maestro** representará una infiltración excepcional y exigirá:

- Completar todos los objetivos exigidos por el contrato.
- No utilizar armas letales ni no letales.
- No utilizar CQC.
- No sufrir ninguna detección confirmada.
- No activar alarmas por detección del agente.

Se permitirán distracciones e interacciones con el entorno que no incumplan esas reglas. Una detección visual confirmada invalidará S++ aunque el agente escape después. Una investigación de ruido sin detección del agente no lo invalidará por sí sola.

Los umbrales numéricos y el equilibrio de bonificaciones y penalizaciones quedan pendientes de pruebas; todavía no hay valores definitivos.

## Cooperación y traición

La experiencia cooperativa permitirá repartir tareas, coordinar distracciones, cubrir rutas y resolver objetivos compartidos. Algunas operaciones añadirán órdenes individuales secretas, privadas para el agente que las recibe.

Esas órdenes podrán pedir recuperar una copia adicional de un archivo o extraer información para una organización rival. En una modalidad específica habrá un traidor cuyo objetivo será **eliminar a todos los demás agentes sin alertar a los guardias enemigos**. El ruido, los cuerpos encontrados y las acciones sospechosas podrán comprometer ese objetivo.

La traición será una modalidad de misión, no una condición de todas las partidas. Cada operación definirá condiciones de victoria para el equipo y, si corresponde, para el traidor, de modo que el conflicto pueda resolverse sin bloquear indefinidamente la partida.

## Base de operaciones

La base de CIO se concibe como una instalación recorrible que los agentes podrán explorar e inspeccionar. Sus espacios tendrán funciones reconocibles:

- **Sala de operaciones:** seleccionar contratos y revisar el objetivo de la operación.
- **Armería y preparación del equipo:** elegir armas, dispositivos y herramientas dentro del límite disponible.
- **Centro de inteligencia:** consultar información sobre objetivos, riesgos y condiciones.
- **Zona de despliegue:** preparar la salida hacia la misión elegida.

Recorrer estos lugares conectará la preparación con la operación y dará presencia física a la organización.

## Misiones y rejugabilidad

Los contratos variarán objetivos, uso permitido de la fuerza, condiciones especiales y requisitos de extracción. Las configuraciones de enemigos, rutas, herramientas disponibles y módulos de escenario crearán nuevas decisiones al repetir una instalación.

Los elementos roguelike se plantean mediante variaciones entre operaciones y adaptación al equipo y a las circunstancias. Su alcance concreto se validará con pruebas; no se presupone un sistema de generación completamente aleatoria.

La duración objetivo de una misión será aproximadamente de **6 a 9 minutos**, ajustable después de probar el ritmo de preparación, infiltración y extracción.

## Multijugador

El objetivo es que varios agentes participen en una misma operación, colaboren y afronten los conflictos que introduzcan las órdenes secretas. El modo por **red local (LAN)** permitirá que jugadores conectados a la misma red compartan una partida desde sus computadores.

Se buscará una experiencia coherente para todo el equipo: observar la misma operación, reconocer el efecto de las acciones de los compañeros y conocer el resultado al finalizar. Esta modalidad sigue planificada y aún no está disponible.

## Alcance del videojuego

Se priorizará una misión jugable con infiltración, enemigos, interacción, puntuación y un primer escenario de cooperación. Después se ampliarán mapas, armas y contratos según lo que las pruebas demuestren útil para la experiencia.

El diseño se desarrollará por entregas verificables. Las características de este documento son objetivos de diseño y se actualizarán conforme se implementen y prueben.
