#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <ftxui/screen/color.hpp>

#include <string>
#include <vector>
#include <sstream>
#include "commands/commands.hpp"

using namespace ftxui;

const std::size_t MAX_VISIBLE_LINES = 20;

ParsedCommand parse_command(const std::string& line) {

    ParsedCommand command;

    std::istringstream stream(line);

    stream >> command.name;

    std::string argument;

    while (stream >> argument) {
        command.args.push_back(argument);
    }

    return command;
}

bool execute_command(const ParsedCommand& parsed, AppState& state) {

    for (const auto& command : commands) {

        if (command.name == parsed.name) {

            command.handler(
                parsed.args,
                state
            );

            return true;
        }
    }

    return false;
}

int main() {

    auto app = App::Fullscreen();

    // Leer texto desde la seccion de consola
    std::string command_line;
    InputOption input_options;
    AppState state;

    // Registrar comandos en AppState
    register_basic_commands();

    // Dar color al placeholder del input
    input_options.transform = [](InputState state) {

        if (state.is_placeholder) {
            return state.element
                | color(Color::GrayDark)
                | bgcolor(Color::Black);
        }

        return state.element
            | color(Color::White)
            | bgcolor(Color::Black);
    };

    // Ejecuta el comando al presionar enter
    input_options.on_enter = [&] {
        if (command_line.empty()) {
            return;
        }

        state.console_lines.push_back(
            "> " + command_line
        );

        ParsedCommand parsed = parse_command(command_line);

        bool found =
            execute_command(parsed, state);


        if (!found) {

            state.console_lines.push_back(
                "Comando no encontrado: " +
                parsed.name
            );
        }


        if (state.should_exit) {
            app.Exit();
        }

        command_line.clear();
    };

    // Crea el input
    auto input = Input(
        &command_line,
        "Escribe un comando...",
        input_options
    );


    // Renderizado de las secciones
    auto renderer = Renderer(input, [&] {

        Elements console_elements;

        // Calcula y renderiza solo las primeras 20 lineas
        std::size_t start =
            state.console_lines.size() > MAX_VISIBLE_LINES ? state.console_lines.size() - MAX_VISIBLE_LINES : 0;

        for (std::size_t i = start; i < state.console_lines.size(); ++i) {
            console_elements.push_back(
                text(" " + state.console_lines[i])
            );
        }

        // Crea los elementos de la consola, cancion y cola
        auto input_line = hbox({
            text(" > ") | bold,
            input->Render() | flex
        });

        auto console = vbox({
            text("Consola") | center | bold,
            separator(),
            vbox(console_elements) | flex,
            separator(),
            input_line
        }) | border;

        auto song = vbox({
            text("Cancion") | center | bold,
            filler()
        }) | border | xflex_grow_factor(2);

        auto queue = vbox({
            text("Cola") | center | bold,
            filler()
        }) | border | xflex_grow_factor(1);

        auto top = hbox({
            song,
            queue
        });

        return vbox({
            top | flex_grow_factor(1),
            console | flex_grow_factor(2)
        });
    });

    app.Loop(renderer);

    return 0;
}