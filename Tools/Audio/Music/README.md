# Música original de NO TRACE

Las tres composiciones y la capa de tensión se sintetizan desde cero mediante `Compose.py`. El proceso no decodifica ni utiliza `Content/Musics/Music.mp3`, `Space.mp3`, efectos de sonido ni fuentes de la biblioteca CC0. No hay samples, melodías importadas ni modelos externos de generación de audio. El resultado es audio musical real, con partitura, motivos propios, evolución armónica y mezcla; no son marcadores temporales.

| Recurso | Uso | Duración | Pulso | Forma |
|---|---|---|---|---|
| `OperacionEncubierta.ogg` | Menú, «Operación encubierta» | 3:12 | 60 BPM | 48 compases |
| `PreparacionTactica.ogg` | Lobby, «Preparación táctica» | 3:12 | 75 BPM | 60 compases |
| `InfiltracionActiva.ogg` | Misión, «Infiltración activa» | 3:12 | 90 BPM | 72 compases |
| `InfiltracionTension.ogg` | Capa de alerta de la misión | 3:12 | 90 BPM | 72 compases |

Todos los recursos son Ogg Vorbis, estéreo, 48.000 Hz y exactamente 9.216.000 frames. `ResourceManager::FetchMusic` ya abre rutas bajo `Content/Musics/` mediante `sf::Music`; el motor reproduce estos archivos por streaming. Las herramientas de esta carpeta no implementan un segundo reproductor.

La identidad común utiliza re menor, novenas suspendidas, inversiones lentas y el semitono frigio en momentos concretos. La paleta consta de sintetizadores aditivos de arco oscuro, graves de ataque lento, teclas amortiguadas, aire filtrado y percusión pequeña y apagada. Las doce secciones de dieciséis segundos tienen inversiones y densidades distintas. El menú desarrolla una clave melódica pausada; el lobby incorpora un pulso de coordinación y otra melodía; la misión reduce el primer plano, deja huecos entre acentos y emplea un motivo grave más tenso. La capa adaptativa añade un ostinato contenido sobre exactamente los mismos acordes y posiciones temporales de la misión.

`Score.json` conserva notas MIDI, secciones, tiempos, ganancias, panoramas y semillas de todos los eventos. Las semillas identifican generadores de ruido y fases iniciales, no recursos externos. La partitura es independiente del audio histórico del proyecto. La salida pertenece al material original del proyecto y se distribuye conforme a `LICENSE.md` (NO TRACE, todos los derechos reservados, Carlos Cortina). NumPy, SciPy y soundfile/libsndfile son herramientas de cálculo y codificación; no son fuentes musicales y sus licencias no se trasladan al audio creado.

## Reproducir el proceso

Se requiere Python 3.12 de 64 bits y las dependencias de `Tools/Audio/requirements.txt`; el Python 3.14 que pueda aparecer en PATH no puede cargar las extensiones cp312 ya instaladas. Desde la raíz del proyecto, con el ejecutable Python 3.12 instalado:

```powershell
py -3.12 Tools/Audio/Music/Compose.py --python-deps build/qa-cierre-eventos-20261017/audio-python
py -3.12 Tools/Audio/Music/Verify.py --python-deps build/qa-cierre-eventos-20261017/audio-python
```

`Compose.py` sobrescribe exclusivamente sus cuatro Ogg y sus metadatos bajo `Content/Musics/NoTrace/`, además de `Score.json` en esta carpeta. La síntesis PCM es determinista con las versiones registradas en requirements. El contenedor Vorbis puede cambiar de bytes entre versiones o ejecuciones debido a su serial interno; `sourcePcmSha256` identifica el PCM de síntesis y `decodedPcm16Sha256` el audio final decodificado. La codificación se realiza por bloques para acotar el buffer del codificador.

## Bucles, mezcla y sincronía

Cada evento se añade a una línea temporal circular: un sonido que cruza el final continúa al principio. La reverberación se calcula con convolución circular y el filtrado sobre el espectro periódico completo; no se corta la cola ni se añade un silencio artificial entre vueltas. La última sección vuelve a la armonía inicial. El verificador decodifica el Vorbis final y exige duración y frecuencia exactas, salto de muestra menor de 0,006 y diferencia de energía menor de 6 dB entre los primeros y últimos 500 ms.

Los objetivos de loudness estimada del archivo son −23 LUFS en menú, −24 en lobby, −27 en misión y −29 en tensión. Estos niveles preceden al control de volumen del motor. La capa adaptativa debe arrancar al mismo tiempo y en la misma posición que la base, con ganancia cero sin alerta; se modifica gradualmente su ganancia. No debe reiniciarse al cambiar de alerta. Ninguna de las dos capas crea eventos acústicos para la IA.

`Verify.py` verifica hashes, formato, cuadrícula compartida, ausencia de clipping, pico intersample aproximado a 4×, offset DC, correlación estéreo, huecos silenciosos, variedad espectral por sección y diferencias de contenido entre recursos. El cálculo de loudness aplica filtros K y puertas absoluta/relativa sobre ventanas de 400 ms cada 100 ms; se declara estimado y no es una medición certificada. `verification.json` conserva resultados y límites de cada comprobación.

## Escucha y aceptación

`Content/Musics/NoTrace/gallery.html` es una galería portátil con reproducción y bucle de cada archivo, créditos y enlaces a los manifiestos. No descarga recursos externos. Escuchar la capa de tensión sola sirve para revisar su material; su mezcla prevista incluye siempre la base de misión y ganancias contenidas.

La verificación numérica no acredita escucha humana, calidad artística, fatiga musical, mezcla con pasos/disparos ni transiciones dentro del juego. La aceptación requiere escuchar un cierre completo de cada bucle, el tránsito entre escenas y los cambios de alerta en ejecución. Esas pruebas deben documentarse con honestidad en la entrega de integración; la existencia de esta galería no las sustituye.
