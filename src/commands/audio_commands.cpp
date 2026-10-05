#include "commands.h"
#include "../audio/audio_player.h"

static AudioPlayer player;

void register_audio_commands() {

    commands.push_back({
        "play",
        "play <archivo>",
        "Reproduce una cancion",
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


int cmd_play(const std::vector<std::string>& args, AppState& state) {

    if (args.empty()) {

        state.console_lines.push_back(
            "Uso: play <archivo>"
        );

        return 1;
    }

    if (!player.play(args[0])) {

        state.console_lines.push_back(
            "No se pudo reproducir: " + args[0]
        );

        return 1;
    }

    state.console_lines.push_back(
        "Reproduciendo: " + args[0]
    );

    return 0;
}


int cmd_pause(const std::vector<std::string>&, AppState& state) {

    if (!player.pause()) {

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


int cmd_resume(const std::vector<std::string>&, AppState& state) {

    if (!player.resume()) {

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


int cmd_stop(const std::vector<std::string>&, AppState& state) {

    if (!player.stop()) {

        state.console_lines.push_back(
            "No hay una cancion reproduciendose"
        );

        return 1;
    }

    state.console_lines.push_back(
        "Reproduccion detenida"
    );

    return 0;
}