# Biblioteca de grabaciones de NO TRACE

`Sources.json` declara las grabaciones nuevas verificadas con CC0 y los hashes de sus descargas. `Library.json` asigna tomas e intervalos concretos a acciones y materiales. Los archivos antiguos y las primeras selecciones rechazadas no son entradas de estas herramientas.

Las fuentes originales se conservan en una carpeta QA ignorada por Git. Para obtener las fuentes que falten desde sus páginas primarias y verificar los hashes:

```powershell
python Tools/Audio/PrepareSources.py --fetch
```

Para preparar los WAV PCM16 de 48 kHz se necesita Python 3.12 y las bibliotecas de `requirements.txt`. Pueden instalarse en una carpeta QA del proyecto sin modificar el Python global:

```powershell
python -m pip install --target build/qa-cierre-eventos-20261017/audio-python -r Tools/Audio/requirements.txt
python Tools/Audio/BuildLibrary.py
python Tools/Audio/GenerateCatalog.py
python Tools/Audio/VerifyLibrary.py
python Tools/Audio/UpdateInventory.py
```

Para actualizar únicamente manifiesto, créditos y galerías después de retirar una receta, usa `python Tools/Audio/BuildLibrary.py --metadata-only`: verifica los hashes y PCM existentes sin reescribir los WAV. El slot de red `Cabinet` permanece inactivo con count0; su receta y procedencia histórica están en `Retired.json`, y la fuente original sigue en `Sources.json`. El paquete activo contiene178 WAV de88familias, con90grabaciones utilizadas de91conservadas.

El procesamiento recorta grabaciones, elimina DC, filtra infrasonidos, convierte la frecuencia de muestreo y aplica fundidos y ajustes de nivel. Los ambientes emplean una transición entre los extremos para permitir un bucle largo. No genera tonos ni ruido, no cambia el tono de una muestra para aparentar variantes y no incorpora recursos rechazados como capas.

La salida está en `Content/Audio/NoTrace`. Cada archivo tiene procedencia, licencia, intervalo de origen, procesamiento, nivel y hashes en `manifest.json`. Las vistas previas HQ de Freesound se identifican como tales: convertirlas a WAV no recupera información del original. Las tomas y los intervalos de una misma grabación permiten verificar qué contactos físicos se han seleccionado.

`Content/Audio/NoTrace/gallery.html` permite escuchar los recursos individuales con sus créditos; `Tools/Audio/TestListening` contiene cuatro montajes y su manifiesto de tiempos. La documentación ampliada se conserva en `docs/audio-redesign-2026-10-18`. Los recursos CC0 preparados siguen necesitando revisión humana para aceptar el estilo y la mezcla dentro de la partida. Las comprobaciones de picos, PCM y hashes no equivalen a una escucha.

La galería no modifica el gameplay. Los eventos acústicos de IA siguen siendo responsabilidad de la simulación; cargar o reproducir un archivo no genera nuevos eventos de ruido.

El generador sintético anterior de silenciadores se retira. Las nuevas herramientas sólo recortan grabaciones CC0 verificadas. El inventario de producción exige todas las variantes, el manifiesto, licencia, créditos y galería portátil; una variante ausente impide publicar y validar una distribución incompleta.

`TestListening/` contiene cuatro montajes de 15–30 segundos con tiempos y hashes para revisar familias. Concatenan únicamente grabaciones activas; no acreditan escucha ni mezcla simultánea en partida. El gatillo vacío tiene dos grabaciones propias de pistola real y usa el enfriamiento autoritativo del arma, sin crear un proyectil ni ruido de IA.
