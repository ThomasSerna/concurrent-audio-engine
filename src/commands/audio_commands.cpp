//Include of required files
#include "commands.h"
#include "playback/playback_engine.h"
#include "playlist/playlist.h"

namespace {

//Reviews that engine and playlist are
//not Null pointers
bool engine_ready(AppState& state) {
    if (state.engine == nullptr || state.playlist == nullptr) {
        state.console_lines.push_back("El motor de reproduccion no esta inicializado");
        return false;
    }
    return true;
}

}  

//Adds the 4 commands to the list
void register_audio_commands() {

    commands.push_back({
        "play",
        "play [archivo]",
        "Reproduce un archivo (lo agrega a la lista) o la lista actual",
        cmd_play
    });

    commands.push_back({
        "pause",
        "pause",
        "Pausa la cancion actual",
        cmd_pause
    });

    commands.push_back({
        "resume",
        "resume",
        "Continua la cancion pausada",
        cmd_resume
    });

    commands.push_back({
        "stop",
        "stop",
        "Detiene la reproduccion",
        cmd_stop
    });
}

//Adds the song and then the path to those args
//and then plays the song in the list
int cmd_play(const std::vector<std::string>& args, AppState& state) {

    if (!engine_ready(state)) {
        return 1;
    }

    if (!args.empty()) {
        const auto id = add_path_from_args(args, state);
        if (!id) {
            return 1;
        }
        state.playlist->select(*id);
    } else if (!state.playlist->current()) {
        //Without any track to read more restarts since the first song
        const auto snapshot = state.playlist->snapshot();
        if (snapshot.tracks.empty()) {
            state.console_lines.push_back("La lista esta vacia. Usa: add <archivo>");
            return 1;
        }
        state.playlist->select(snapshot.tracks.front().id);
    }

    state.engine->play();

    if (const auto current = state.playlist->current()) {
        state.console_lines.push_back("Reproduciendo: " + current->title);
    }

    return 0;
}

//Command for pausing with commetaries
//for indicating the user what is happening
int cmd_pause(const std::vector<std::string>&, AppState& state) {

    if (!engine_ready(state)) {
        return 1;
    }

    if (!state.engine->pause()) {

        state.console_lines.push_back(
            "No hay una cancion reproduciendose"
        );

        return 1;
    }

    state.console_lines.push_back(
        "Reproduccion pausada"
    );

    return 0;
}

//Command for resuming with commetaries
//for indicating the user what is happening
int cmd_resume(const std::vector<std::string>&, AppState& state) {

    if (!engine_ready(state)) {
        return 1;
    }

    if (!state.engine->resume()) {

        state.console_lines.push_back(
            "No hay una cancion pausada"
        );

        return 1;
    }

    state.console_lines.push_back(
        "Reproduccion reanudada"
    );

    return 0;
}

//Command for stopping with commetaries
//for indicating the user what is happening
int cmd_stop(const std::vector<std::string>&, AppState& state) {

    if (!engine_ready(state)) {
        return 1;
    }

    if (!state.engine->stop()) {

        state.console_lines.push_back(
            "No hay una cancion reproduciendose"
        );

        return 1;
    }

    state.console_lines.push_back(
        "Reproduccion detenida (usa play para continuar)"
    );

    return 0;
}
