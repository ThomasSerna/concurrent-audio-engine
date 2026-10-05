#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>
#include <vector>
#include <sstream>
#include "commands/commands.hpp"

using namespace ftxui;

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
    std::vector<std::string> console_lines;
    InputOption input_options;
    AppState state;

    input_options.on_enter = [&] {
        if (command_line.empty()) {
            return;
        }

        state.console_lines.push_back(
            "> " + command_line
        );

        ParsedCommand parsed =
            parse_command(command_line);


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

    auto input = Input(
        &command_line,
        "Escribe un comando...",
        input_options
    );



    // Renderizado de las secciones
    auto renderer = Renderer(input, [&] {

        Elements console_elements;
        for (const auto& line : console_lines) {

            console_elements.push_back(
            text(" " + line)
        );
        }

        auto console = vbox({
            text("Consola") | center,
            separator(),
            vbox(console_elements),
            filler(),
            hbox({
                text(" > "),
                input->Render()
            })
        }) | border;

        // Secciones
        auto song = text("Cancion") | center | border| flex;
        auto queue = text("Cola") | center | border| flex;

        // Barra superior
        auto top = hbox({
            song | flex_grow_factor(2),
            queue | flex_grow_factor(1)
        });

        return vbox({
            top | flex_grow_factor(1),
            console | flex_grow_factor(2)
        });
    });

    app.Loop(renderer);

    return 0;
}