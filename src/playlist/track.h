#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace playlist {

// Identificador estable de una pista. Nunca se reutiliza durante la vida de la
// Playlist, así que a diferencia de un índice no queda "apuntando a otra
// canción" cuando alguien inserta, elimina o reordena.
using TrackId = std::uint64_t;

// Tipo valor: Playlist siempre devuelve COPIAS. Ningún hilo conserva
// referencias ni punteros a elementos internos, de modo que eliminar una pista
// no puede dejar al hilo decodificador con memoria liberada.
struct Track {
    TrackId id{};
    std::filesystem::path path;
    std::string title;
};

}  // namespace playlist