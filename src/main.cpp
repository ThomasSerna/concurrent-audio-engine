//Include of external programs needed for this
#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

//Include of required tools
#include <algorithm>
#include <string>
#include <vector>
#include <sstream>

//Include of required programs
#include "commands/commands.h"
#include "playback/playback_engine.h"
#include "playlist/path_utf8.h"
#include "playlist/playlist.h"

//Stablishing the namespace for simplifying processes 
using namespace ftxui;

//Stablishing the visible lines in console
const std::size_t MAX_VISIBLE_LINES = 20;

//Parse the command given by the user
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

//Execute the command written down by the user
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
    //Executing the player
    auto app = App::Fullscreen();

    //Reading the text indicated in console
    std::string command_line;
    InputOption input_options;
    AppState state;

    //Shared playlist plus engine player on its own thread
    //The engine gets destroyed before the playlist for good practices
    playlist::Playlist playlist;
    PlaybackEngine engine(playlist);
    state.playlist = &playlist;
    state.engine = &engine;

    //The engine thread asks to redraw the console when a track end
    engine.set_redraw_callback([&] { app.PostEvent(Event::Custom); });
    engine.start();

    // Register commands in AppState
    register_basic_commands();
    register_audio_commands();
    register_playlist_commands();

    // Give color to the placeholder input
    input_options.transform = [](InputState input_state) {

        if (input_state.is_placeholder) {
            return input_state.element
                | color(Color::GrayDark)
                | bgcolor(Color::Black);
        }

        return input_state.element
            | color(Color::White)
            | bgcolor(Color::Black);
    };

    //Executing the command when clicking enter
    //and returning an error message if it is not
    //found
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
            engine.shutdown();
            app.Exit();
        }

        command_line.clear();
    };

    //Create the input
    auto input = Input(
        &command_line,
        "Escribe un comando...",
        input_options
    );


    //Renderizing of the sections
    auto renderer = Renderer(input, [&] {

        //Messages that the thread of the engine leave for the console
        for (auto& message : engine.take_messages()) {
            state.console_lines.push_back(std::move(message));
        }

        Elements console_elements;

        //Calculates and renderizes only the first 20 lines
        std::size_t start =
            state.console_lines.size() > MAX_VISIBLE_LINES ? state.console_lines.size() - MAX_VISIBLE_LINES : 0;

        for (std::size_t i = start; i < state.console_lines.size(); ++i) {
            console_elements.push_back(
                text(" " + state.console_lines[i])
            );
        }

        //Crates elements of the console, songs and tail
        auto input_line = hbox({
            text(" > ") | bold | color(Color::DarkCyan),
            input->Render() | flex
        });

        auto console = vbox({
            text("Consola") | center | bold | color(Color::CyanLight),
            separator(),
            vbox(console_elements) | flex,
            separator(),
            input_line
        }) | border;

        //Pannel "Song": current track and state of the player
        Elements song_elements;
        if (const auto current = playlist.current()) {
            std::string status = "Detenido";
            if (!engine.user_stopped()) {
                switch (engine.state()) {
                    case PlayerState::PLAYING: status = "Reproduciendo"; break;
                    case PlayerState::PAUSED:  status = "Pausado"; break;
                    case PlayerState::STOPPED: status = "Cargando..."; break;
                }
            }
            song_elements.push_back(text(" " + current->title) | bold);
            song_elements.push_back(text(" " + playlist::to_utf8(current->path)) | dim);
            song_elements.push_back(text(" Estado: " + status));
            if (const auto next = playlist.peek_next()) {
                song_elements.push_back(text(" Siguiente: " + next->title) | dim);
            }
        } else {
            song_elements.push_back(text(" Sin cancion actual") | dim);
        }

        //Panel "Tail": only a reading consistent list obtained from 
        //snapshot function
        Elements queue_elements;
        const auto snapshot = playlist.snapshot();
        const std::size_t shown = std::min(snapshot.tracks.size(), MAX_VISIBLE_LINES);
        for (std::size_t i = 0; i < shown; ++i) {
            const auto& track = snapshot.tracks[i];
            const bool is_current = snapshot.current && *snapshot.current == track.id;
            auto line = text((is_current ? " > " : "   ") + std::to_string(i + 1) + ". " + track.title);
            queue_elements.push_back(is_current ? line | bold | color(Color::Green) : line);
        }
        if (snapshot.tracks.size() > shown) {
            queue_elements.push_back(
                text("   ... y " + std::to_string(snapshot.tracks.size() - shown) + " mas") | dim);
        }
        if (queue_elements.empty()) {
            queue_elements.push_back(text(" La cola esta vacia") | dim);
        }

        auto song = vbox({
            text("Cancion") | center | bold | color(Color::CyanLight),
            separator(),
            vbox(song_elements) | flex
        }) | border | xflex_grow_factor(2);

        auto queue = vbox({
            text("Cola de reproduccion") | center | bold | color(Color::CyanLight),
            separator(),
            vbox(queue_elements) | flex
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

    // Shutdowns the engine when the process is finished by using Ctrl + c
    engine.shutdown();

    return 0;
}
