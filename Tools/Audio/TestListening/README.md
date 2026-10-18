# Revisión auditiva pendiente

Estos montajes concatenan recortes de los WAV reales del catálogo para facilitar una primera escucha comparativa. No son capturas de gameplay, ni una prueba de la mezcla simultánea, la oclusión o la distancia. El manifiesto registra procedencia, tiempos y hashes. Los recursos individuales y todas las variantes se revisan desde `Content/Audio/NoTrace/gallery.html`.

```powershell
python Tools/Audio/TestListening/BuildMontages.py
```

Las cuatro secuencias duran entre 15 y 30 segundos. Sólo estos montajes reducen las fuentes estéreo a mono y acortan los ambientes a cuatro segundos; los recursos activos conservan sus canales y duración. Las pausas son silencio digital, sin síntesis de sonidos de reemplazo.

Estado: análisis PCM y procedencia verificados; **escucha perceptiva pendiente**. No hay revisor ni aceptación auditiva registrada. Reproducir en el equipo sin percepción del revisor no cambia ese estado. Tras escuchar, registrar por archivo el revisor, fecha, variante y problemas concretos; después comprobar en partida las fuentes simultáneas, buses, prioridad y oclusión.
