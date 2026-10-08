#pragma once

#include <optional>
#include <string>
#include <vector>

#include "playlist/track.h"

// Declaraciones adelantadas: los comandos solo necesitan punteros.
namespace playlist {
class Playlist;
}
class PlaybackEngine;

struct AppState {

    std::vector<std::string> console_lines;

    bool should_exit = false;

    // Se asignan en main() antes de ejecutar cualquier comando.
    playlist::Playlist* playlist = nullptr;
    PlaybackEngine* engine = nullptr;
};

using CommandHandler = int (*)(const std::vector<std::string>& args, AppState& state);

struct Command {
    std::string name;
    std::string usage;
    std::string description;
    CommandHandler handler;
};
struct ParsedCommand {
    std::string name;
    std::vector<std::string> args;
};

inline std::vector<Command> commands;

ParsedCommand parse_command(const std::string& line);

bool execute_command(const ParsedCommand& command, AppState& state);

int cmd_help(const std::vector<std::string>& args, AppState& state);
int cmd_clear(const std::vector<std::string>& args, AppState& state);
int cmd_exit(const std::vector<std::string>& args, AppState& state);

// Reproduccion
int cmd_play(const std::vector<std::string>& args, AppState& state);
int cmd_stop(const std::vector<std::string>& args, AppState& state);
int cmd_resume(const std::vector<std::string>& args, AppState& state);
int cmd_pause(const std::vector<std::string>& args, AppState& state);

// Playlist
int cmd_add(const std::vector<std::string>& args, AppState& state);
int cmd_list(const std::vector<std::string>& args, AppState& state);
int cmd_next(const std::vector<std::string>& args, AppState& state);
int cmd_prev(const std::vector<std::string>& args, AppState& state);
int cmd_jump(const std::vector<std::string>& args, AppState& state);
int cmd_remove(const std::vector<std::string>& args, AppState& state);
int cmd_move(const std::vector<std::string>& args, AppState& state);
int cmd_qclear(const std::vector<std::string>& args, AppState& state);

// Valida la ruta (args unidos por espacios) y la agrega a la playlist.
// Devuelve el id de la pista, o nullopt si fallo (ya deja el mensaje en consola).
std::optional<playlist::TrackId> add_path_from_args(const std::vector<std::string>& args,
                                                    AppState& state);

void register_basic_commands();
void register_audio_commands();
void register_playlist_commands();
