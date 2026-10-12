//Include for father file
#include "commands.h"

//Include of required tools
#include <charconv>
#include <filesystem>
#include <system_error>

//Include of required files
#include "playback/playback_engine.h"
#include "playlist/path_utf8.h"
#include "playlist/playlist.h"

namespace fs = std::filesystem;

namespace {

//Reviews that the engine and playlist are ready
bool ready(AppState& state) {
    if (state.playlist == nullptr || state.engine == nullptr) {
        state.console_lines.push_back("La playlist no esta inicializada");
        return false;
    }
    return true;
}

// Takes care of joining the args of the 
// command given by the user using the space
// as the separator 
std::string join_args(const std::vector<std::string>& args) {
    std::string out;
    for (const auto& arg : args) {
        if (!out.empty()) {
            out += ' ';
        }
        out += arg;
    }
    return out;
}

// Converts the command given by the user into numbers that are
// more understandable
std::optional<std::size_t> parse_position(const std::string& text) {
    std::size_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || value == 0) {
        return std::nullopt;
    }
    return value;
}

//The commands work with ids so the list changes between medias without affecting
//the other tracks by using these ids
std::optional<playlist::TrackId> id_at(const playlist::Playlist& list, std::size_t position) {
    const auto snapshot = list.snapshot();
    if (position == 0 || position > snapshot.tracks.size()) {
        return std::nullopt;
    }
    return snapshot.tracks[position - 1].id;
}

}  

//All possible commands that can be used in the playlist
void register_playlist_commands() {

    commands.push_back({"add", "add <archivo>", "Agrega una cancion al final de la lista", cmd_add});
    commands.push_back({"list", "list", "Muestra la lista de reproduccion", cmd_list});
    commands.push_back({"next", "next", "Salta a la siguiente cancion", cmd_next});
    commands.push_back({"prev", "prev", "Vuelve a la cancion anterior", cmd_prev});
    commands.push_back({"jump", "jump <n>", "Reproduce la cancion en la posicion n", cmd_jump});
    commands.push_back({"remove", "remove <n>", "Elimina la cancion en la posicion n", cmd_remove});
    commands.push_back({"move", "move <desde> <hasta>", "Mueve una cancion a otra posicion", cmd_move});
    commands.push_back({"qclear", "qclear", "Vacia la lista de reproduccion", cmd_qclear});
}

//Takes the path to the track from the argument
//given in the console and verifies if it is
//real before even try adding it
std::optional<playlist::TrackId> add_path_from_args(const std::vector<std::string>& args,
                                                    AppState& state) {
    if (!ready(state)) {
        return std::nullopt;
    }
    if (args.empty()) {
        state.console_lines.push_back("Uso: add <archivo>");
        return std::nullopt;
    }

    std::error_code ec;
    const fs::path path = fs::absolute(playlist::from_utf8(join_args(args)), ec);
    if (ec || !fs::is_regular_file(path, ec)) {
        state.console_lines.push_back("No existe el archivo: " + join_args(args));
        return std::nullopt;
    }

    const playlist::TrackId id = state.playlist->add(path);
    state.console_lines.push_back(
        "Agregada (#" + std::to_string(state.playlist->size()) + "): " +
        playlist::to_utf8(path.stem()));
    return id;
}

//Adds the path to the list of tracks 
int cmd_add(const std::vector<std::string>& args, AppState& state) {
    return add_path_from_args(args, state) ? 0 : 1;
}

//Takes a list of all the tracks and numerates them in order
int cmd_list(const std::vector<std::string>&, AppState& state) {
    if (!ready(state)) {
        return 1;
    }

    const auto snapshot = state.playlist->snapshot();
    if (snapshot.tracks.empty()) {
        state.console_lines.push_back("La lista esta vacia");
        return 0;
    }

    state.console_lines.push_back("Lista de reproduccion:");
    for (std::size_t i = 0; i < snapshot.tracks.size(); ++i) {
        const auto& track = snapshot.tracks[i];
        const bool is_current = snapshot.current && *snapshot.current == track.id;
        state.console_lines.push_back(
            std::string(is_current ? "  > " : "    ") + std::to_string(i + 1) + ". " + track.title);
    }
    return 0;
}

//Goes to the next song
int cmd_next(const std::vector<std::string>&, AppState& state) {
    if (!ready(state)) {
        return 1;
    }
    if (!state.playlist->current()) {
        state.console_lines.push_back("No hay una cancion actual");
        return 1;
    }

    const auto next = state.playlist->advance();
    if (!next) {
        state.console_lines.push_back("Fin de la lista");
        return 0;
    }
    state.engine->play();
    state.console_lines.push_back("Siguiente: " + next->title);
    return 0;
}

//Goes to the previous song
int cmd_prev(const std::vector<std::string>&, AppState& state) {
    if (!ready(state)) {
        return 1;
    }

    const auto previous = state.playlist->previous();
    if (!previous) {
        state.console_lines.push_back("La lista esta vacia");
        return 1;
    }
    state.engine->play();
    state.console_lines.push_back("Anterior: " + previous->title);
    return 0;
}

//Process before going to the next song to verify it is playable
int cmd_jump(const std::vector<std::string>& args, AppState& state) {
    if (!ready(state)) {
        return 1;
    }
    const auto position = args.empty() ? std::nullopt : parse_position(args[0]);
    if (!position) {
        state.console_lines.push_back("Uso: jump <n>   (n = posicion en la lista)");
        return 1;
    }
    const auto id = id_at(*state.playlist, *position);
    if (!id || !state.playlist->select(*id)) {
        state.console_lines.push_back("No existe la posicion " + args[0]);
        return 1;
    }
    state.engine->play();
    if (const auto current = state.playlist->current()) {
        state.console_lines.push_back("Reproduciendo: " + current->title);
    }
    return 0;
}

//Removes a song from the list
int cmd_remove(const std::vector<std::string>& args, AppState& state) {
    if (!ready(state)) {
        return 1;
    }
    const auto position = args.empty() ? std::nullopt : parse_position(args[0]);
    if (!position) {
        state.console_lines.push_back("Uso: remove <n>   (n = posicion en la lista)");
        return 1;
    }
    const auto id = id_at(*state.playlist, *position);
    if (!id || !state.playlist->remove(*id)) {
        state.console_lines.push_back("No existe la posicion " + args[0]);
        return 1;
    }
    state.engine->notify_change();
    state.console_lines.push_back("Eliminada la posicion " + args[0]);
    return 0;
}

//Moves a song from the pre stablished order in the list
int cmd_move(const std::vector<std::string>& args, AppState& state) {
    if (!ready(state)) {
        return 1;
    }
    const auto from = args.size() < 2 ? std::nullopt : parse_position(args[0]);
    const auto to = args.size() < 2 ? std::nullopt : parse_position(args[1]);
    if (!from || !to) {
        state.console_lines.push_back("Uso: move <desde> <hasta>");
        return 1;
    }
    const auto id = id_at(*state.playlist, *from);
    if (!id || !state.playlist->move(*id, *to - 1)) {
        state.console_lines.push_back("No existe la posicion " + args[0]);
        return 1;
    }
    state.console_lines.push_back("Movida la posicion " + args[0] + " a " + args[1]);
    return 0;
}

//Empties the list and tells the engine
int cmd_qclear(const std::vector<std::string>&, AppState& state) {
    if (!ready(state)) {
        return 1;
    }
    state.playlist->clear();
    state.engine->notify_change();
    state.console_lines.push_back("Lista vaciada");
    return 0;
}
