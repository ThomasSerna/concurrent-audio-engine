#include "commands.hpp"

void register_basic_commands() {

    commands.push_back({
        "help",
        "help",
        "Muestra los detalles de los comandos del programa",
        cmd_help
    });

    commands.push_back({
        "clear",
        "clear",
        "Limpia la consola",
        cmd_clear
    });

    commands.push_back({
        "exit",
        "exit",
        "Cierra el programa",
        cmd_exit
    });
}

int cmd_clear(const std::vector<std::string>& args, AppState& state) {

    state.console_lines.clear();

    return 0;
}

int cmd_exit(const std::vector<std::string>& args, AppState& state) {

    state.should_exit = true;

    return 0;
}

int cmd_help(const std::vector<std::string>& args, AppState& state) {

    state.console_lines.push_back(
        "Comandos disponibles:"
    );

    for (const auto& command : commands) {

        state.console_lines.push_back(
            "  " +
            command.name +
            " - " +
            command.description
        );
    }

    return 0;
}