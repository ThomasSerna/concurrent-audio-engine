# Concurrent Audio Engine

## Integrantes

* Juan Esteban Palacio Betancur
* Mateo Montoya Ospina
* Thomas Serna Saldarriaga

## Descripción

Reproductor de audio de línea de comandos para Linux/POSIX, desarrollado en C++20 como **Alternativa 2** del Parcial 2 de Sistemas Operativos (*Concurrencia y Sincronización*). La aplicación ofrece una interfaz de terminal de pantalla completa (FTXUI) y permite gestionar una lista de reproducción mientras una canción suena.

El foco del proyecto es el diseño concurrente:

* El **motor de reproducción** corre en un hilo propio y se desacopla de la interfaz.
* La **lista de reproducción** es una estructura compartida protegida con un `pthread_rwlock_t` (problema Lectores-Escritores), de modo que la interfaz puede consultarla mientras el usuario la modifica.
* El **búfer circular** de audio sincroniza un hilo productor (decodificación) con un hilo consumidor (salida), usando mutex y variables de condición, sin espera activa.
* Los **controles** (play, pause, resume, stop, next, prev, jump) llegan de forma asíncrona y se propagan al motor sin bloquear la UI.

La decodificación se realiza con `ffmpeg` y la reproducción con `ffplay`, ambos ejecutados como procesos hijos y comunicados mediante pipes.

## Requisitos

* Linux / POSIX (no compila en Windows; CMake lo rechaza explícitamente)
* GCC o Clang con soporte de C++20
* CMake 3.20 o superior
* Git (CMake descarga FTXUI con `FetchContent` la primera vez)
* `ffmpeg` y `ffplay` disponibles en el `PATH`

En Ubuntu/Debian:

```bash
sudo apt install build-essential cmake git ffmpeg
```

## Compilación y ejecución

```bash
cmake -S . -B build
cmake --build build
./build/concurrent-audio-engine
```

Para compilar solo las pruebas, sin la aplicación (no requiere descargar FTXUI):

```bash
cmake -S . -B build -DBUILD_APPLICATION=OFF
cmake --build build
```

Para ejecutar las pruebas:

```bash
ctest --test-dir build --output-on-failure
```

Para limpiar los archivos compilados:

```bash
rm -rf build
```

## Comandos de la aplicación

Los comandos se escriben en el panel **Consola**, en la parte inferior de la pantalla. Las posiciones (`<n>`) son 1-based, como se muestran en la lista.

|          Comando           |                  Descripción                   |
|:--------------------------:|:----------------------------------------------:|
|           `help`           |   Muestra la ayuda con todos los comandos      |
|          `clear`           |             Limpia la consola                  |
|           `exit`           |        Cierra el programa de forma ordenada    |
|      `play [archivo]`      | Reproduce un archivo (lo agrega) o la lista actual |
|          `pause`           |           Pausa la canción actual              |
|          `resume`          |         Continúa la canción pausada            |
|           `stop`           |   Detiene la reproducción sin avanzar          |
|    `add <archivo>`         |        Agrega una canción al final             |
|           `list`           |         Muestra la lista de reproducción       |
|           `next`           |          Salta a la siguiente canción          |
|           `prev`           |        Vuelve a la canción anterior            |
|       `jump <n>`          |       Reproduce la canción en la posición n    |
|      `remove <n>`         |        Elimina la canción en la posición n     |
|    `move <desde> <hasta>`  |          Mueve una canción de posición        |
|          `qclear`          |          Vacía la lista de reproducción        |

## Componentes

| Módulo | Archivos | Responsabilidad |
|:-:|:-:|:-:|
| Interfaz | `main.cpp`, `commands/` | Renderizado FTXUI, parseo y ejecución de comandos |
| Playlist | `playlist/playlist.{h,cpp}`, `rw_lock.h`, `track.h` | Lista compartida Lectores-Escritores con cursor por ID |
| Motor de reproducción | `playback/playback_engine.{h,cpp}` | Hilo que consume la playlist y decide qué suena |
| Reproductor | `audio/audio_player.{h,cpp}` | Procesos ffmpeg/ffplay, precarga y pipeline productor-consumidor |
| Búfer de audio | `audio/circular_audio_buffer.h` | Búfer circular sincronizado con mutex y condition variables |

## Protocolo de concurrencia

**Playlist.** Los lectores (`current`, `snapshot`, `peek_next`, `size`) comparten el lock; los escritores (`add`, `remove`, `move`, `clear`, `select`, `advance`, `previous`, `on_track_finished`) lo toman en exclusiva. Reglas principales:

* Los métodos devuelven **copias** de las pistas, por lo que nadie conserva referencias a memoria que pueda liberarse.
* Dentro de una sección crítica solo se usan helpers privados `*_locked`, nunca métodos públicos, para no readquirir el lock.
* No se realiza E/S con el lock tomado: la validación de rutas se hace antes de `add()`.
* El lock de la lista y el `signal_mutex_` nunca se mantienen a la vez. El escritor publica el cambio, suelta el lock y después notifica, lo que evita interbloqueos.
* El cursor se guarda por **ID**, no por índice: reordenar o insertar no cambia la pista actual.
* El `pthread_rwlock` se configura con prioridad al escritor (`PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP`) para que las modificaciones no queden bloqueadas por lecturas continuas.

**Carrera «fin natural» vs. «el usuario pulsa Next».** Cada selección tiene una *época* (`selection_epoch`). El motor guarda la época al empezar una pista y, al terminar, llama a `on_track_finished(epoch)`, que avanza **solo si la época no cambió**. Así una pista nunca se salta dos veces.

**Búfer circular.** El productor espera en `writable_` (búfer lleno) y el consumidor en `readable_` (búfer vacío); no hay espera activa. `close()` despierta a ambos lados para que el cierre sea limpio.

**Parada y cierre.** `stop()` y `shutdown()` cierran el búfer, reanudan (`SIGCONT`) y terminan (`SIGTERM`) los procesos, hacen `join` de los hilos y luego recogen los procesos con `waitpid`.

## Primitivas y llamadas al sistema utilizadas

Concurrencia:

```text
std::thread, std::mutex, std::recursive_mutex, std::condition_variable, std::atomic
pthread_rwlock_rdlock(), pthread_rwlock_wrlock(), pthread_rwlock_unlock()
pthread_rwlockattr_setkind_np()   (prioridad al escritor, glibc)
```

Procesos, pipes y señales:

```text
fork(), execlp(), _exit()
pipe2() con O_CLOEXEC, dup2(), read(), write(), close()
waitpid(), kill() con SIGTERM / SIGCONT, sigaction() para ignorar SIGPIPE
access()
```

## Pruebas

| Ejecutable | Qué verifica |
|:-:|:-:|
| `audio-buffer-test` | Transferencia de 2 MiB con un tamaño de búfer no potencia de dos (wrap-around) y que `close()` despierte a un productor bloqueado |
| `playlist-stress-test` | 4 escritores y 4 lectores concurrentes más un motor simulado: ids únicos, cursor válido, época no retrocede y `close()` no produce deadlock |
| `audio-player-smoke-test` | Genera un tono con `ffmpeg` y verifica inicio, pausa, reanudación y parada (usa el backend SDL dummy, sin tarjeta de audio) |

## Estructura

```text
concurrent-audio-engine/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── audio/
│   │   ├── audio_player.h
│   │   ├── audio_player.cpp
│   │   └── circular_audio_buffer.h
│   ├── commands/
│   │   ├── commands.h
│   │   ├── basic_commands.cpp
│   │   ├── audio_commands.cpp
│   │   └── playlist_commands.cpp
│   ├── playback/
│   │   ├── playback_engine.h
│   │   └── playback_engine.cpp
│   └── playlist/
│       ├── playlist.h
│       ├── playlist.cpp
│       ├── rw_lock.h
│       ├── track.h
│       └── path_utf8.h
└── tests/
    ├── audio_buffer_test.cpp
    ├── audio_player_smoke_test.cpp
    └── playlist_stress_test.cpp
```
