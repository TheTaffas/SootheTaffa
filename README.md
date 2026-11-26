# Resonance Suppressor (VST3, macOS 13+)

## Compilar en la nube (GitHub Actions)
1. Crear un repositorio nuevo en github.com (privado o publico).
2. Subir TODO el contenido de esta carpeta, incluida la carpeta oculta `.github`.
   (En la web: "Add file > Upload files" y arrastrar; si `.github` no se ve en Finder, Cmd+Shift+. muestra archivos ocultos.)
3. Pestana "Actions" > "Build VST3 (macOS)" > esperar ~10 min (o "Run workflow").
4. Abrir la corrida terminada > "Artifacts" > bajar `ResonanceSuppressor-macOS-VST3`.

## Instalar
1. Descomprimir; queda `Resonance Suppressor.vst3`.
2. En Terminal (quita la cuarentena de macOS):
   xattr -cr "Resonance Suppressor.vst3"
3. Copiar a `~/Library/Audio/Plug-Ins/VST3/` (o `/Library/Audio/Plug-Ins/VST3/`).
4. Reaper: Options > Preferences > Plug-ins > VST > Re-scan.
5. Add FX > "VST3: Resonance Suppressor (Herny)".

Latencia: 4096 muestras (Reaper la compensa solo).

## Controles
Depth, Sharpness, Selectivity, Attack, Release, Low/High Limit, Max Reduction, Mix, Output.
- DELTA: escucha solo lo que se esta quitando.
- AUTO GAIN: compensa el volumen perdido (hasta +12 dB) para comparar en igualdad de nivel.
- Grafico: espectro de entrada (celeste) y reduccion por frecuencia (naranja).
- MODE: Stereo (L/R linkeados) o Mid/Side (analiza y suprime mid y side por separado).
- LATENCY: Low (2048 muestras, ~43 ms a 48 kHz) o High quality (4096, ~85 ms). Reaper compensa la latencia solo.
- SIDECHAIN: detecta las resonancias en la senal del sidechain y las suprime en la pista principal.
  En Reaper: en la pista del plugin pone 4 canales (Track > Channels), manda otra pista a los canales 3/4
  (Route > Send, "Mix to channels 3/4") y en el plugin abri "Pin connector" para asignar 3/4 a la entrada Sidechain.
  Si no hay nada conectado, el plugin usa la senal principal y lo avisa en el grafico.
