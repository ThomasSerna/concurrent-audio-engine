# Concurrent audio engine

Reproductor de audio en línea de comandos (CLI con interfaz FTXUI) cuyo motor de
reproducción está desacoplado de la gestión concurrente de la lista de reproducción.
Proyecto de Sistemas Operativos (Alternativa 2: Reproductor de Audio y Gestión
Concurrente de Lista de Reproducción).

**Integrantes:** Juan Esteban Palacio, Mateo Montoya, Thomas Serna.

## Requisitos

- Linux o Windows con **WSL (Ubuntu)**. El proyecto usa `pthread`, `fork` y pipes: no compila con el toolchain de Windows.
- `g++` con soporte C++20, `cmake` ≥ 3.20 y `git` (FTXUI se descarga automáticamente).
- `ffmpeg` (incluye `ffplay`): `sudo apt install ffmpeg`.
- Salida de audio funcional en el sistema (en WSL, WSLg).

## Compilar y ejecutar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j
./build/concurrent-audio-engine
```

En CLion: *Settings → Build, Execution, Deployment → Toolchains* → añadir **WSL** (Ubuntu),
seleccionarlo como toolchain del proyecto y recargar CMake.

## Pruebas

```bash
cd build && ctest --output-on-failure
```

- `audio-buffer-test`: búfer circular productor-consumidor (wrap-around y cancelación).
- `playlist-stress-test`: 4 escritores, 4 lectores y un motor simulado sobre la misma lista.
- `audio-player-smoke-test`: inicio, pausa, reanudación y parada reales con ffmpeg/ffplay.

Para detectar condiciones de carrera:
`g++ -std=c++20 -g -fsanitize=thread -pthread -Isrc tests/playlist_stress_test.cpp src/playlist/playlist.cpp -o stress_tsan && ./stress_tsan`

## Comandos

| Comando | Descripción |
|---|---|
| `add <archivo>` | Agrega una canción al final de la lista. |
| `play [archivo]` | Reproduce un archivo (lo agrega) o la pista actual. |
| `pause` / `resume` / `stop` | Pausa, reanuda o detiene la reproducción. |
| `next` / `prev` | Salta a la siguiente / anterior pista. |
| `jump <n>` | Reproduce la pista en la posición n. |
| `list` | Muestra la lista con la pista actual marcada. |
| `remove <n>` | Elimina la pista en la posición n. |
| `move <desde> <hasta>` | Reordena la lista. |
| `qclear` | Vacía la lista. |
| `help`, `clear`, `exit` | Ayuda, limpia la consola, cierra el programa. |

## Arquitectura

Hilos del programa:

- **Hilo de UI (main):** lee comandos y dibuja la interfaz. Nunca espera a la reproducción.
- **Hilo del motor (`PlaybackEngine`):** consume la lista. Bloquea en `wait_for_current()` (variable de condición) y, mientras suena una pista, responde a stop, cambios de selección y fin natural.
- **Hilo productor de `AudioPlayer`:** lee PCM desde `ffmpeg` y lo deposita en un `CircularAudioBuffer`.
- **Hilo consumidor de `AudioPlayer`:** extrae PCM del búfer y lo escribe en el pipe de `ffplay`, que es quien reproduce el sonido.

### Protocolo de concurrencia

**Lista de reproducción (`playlist/playlist.h`)**: problema lectores-escritores con `pthread_rwlock`
con prioridad al escritor.

- Lectores (`current`, `snapshot`, `size`, `peek_next`) toman lock compartido; escritores (`add`, `remove`, `move`, `clear`, `select`, `advance`, `previous`, `on_track_finished`) toman lock exclusivo.
- Todo método devuelve copias: ningún hilo conserva referencias internas, así que borrar una pista no deja punteros colgando.
- El cursor de reproducción se guarda por **ID estable**, no por índice: reordenar o insertar no cambia la pista actual.
- Los escritores publican la versión, sueltan el lock y luego notifican; `lock_` y `signal_mutex_` nunca se mantienen a la vez, por lo que no hay interbloqueo.
- El fin natural de pista usa `on_track_finished(epoch)`, una comparación y avance atómicos: si el usuario ya cambió la selección, no se salta una canción dos veces.

**Búfer de audio (`audio/circular_audio_buffer.h`)**: `mutex` + dos variables de condición
(`readable_`, `writable_`). El productor espera si el búfer está lleno y el consumidor si está vacío; no hay espera activa. `close()` despierta a ambos lados.

**Control (`playback/playback_engine.cpp`)**: los comandos de UI llaman directamente a `AudioPlayer`
(protegido por un `recursive_mutex`) y actualizan un contador de paradas y la época de selección.
El motor duerme en una variable de condición y se despierta de inmediato ante cambios; el sondeo de 100 ms sólo sirve para detectar que `ffplay` terminó solo.

**Preparación de la siguiente pista**: mientras suena una pista, el motor llama a
`AudioPlayer::prefetch()` con la siguiente de la lista. Eso lanza ya su decodificador
(`ffmpeg`), que llena su pipe y queda listo. Cuando la pista actual termina, `play()` reutiliza
ese decodificador en vez de arrancar uno nuevo. Si la lista cambió y la pista precargada ya no es
la siguiente, el prefetch se descarta. Limitación: `ffplay` se reinicia en cada pista, así que
todavía puede haber un pequeño corte entre canciones.

**Parada de procesos**: `ffmpeg` y `ffplay` se pausan con `SIGSTOP`. Antes de terminarlos se reanudan con `SIGCONT`, porque un proceso detenido no procesa `SIGTERM`.

## Estructura

```
src/
  main.cpp                    interfaz FTXUI y panel de consola
  commands/                   parseo y comandos de consola
  playlist/                   Playlist, RwLock (pthread_rwlock), Track
  playback/                   PlaybackEngine (hilo del motor)
  audio/                      AudioPlayer (ffmpeg | ffplay) y CircularAudioBuffer
tests/                        pruebas de búfer, playlist y reproductor
```
