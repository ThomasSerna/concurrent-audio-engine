//Include guard for reading file only once
#pragma once

//Include of required tools
#include <optional>
#include <string>
#include <vector>

//Include of required programs
#include "playlist/track.h"

//Indicates that it will be using playlist
//without having to load the entire file
//reducing load in the proccess
namespace playlist {
class Playlist;
}
class PlaybackEngine;

//Indicates everything the process must remember
//while executing such as: console lines, if the
//process has to end, in which part of the list
//is at and the engine executing the process
struct AppState {

    std::vector<std::string> console_lines;

    bool should_exit = false;

    playlist::Playlist* playlist = nullptr;
    PlaybackEngine* engine = nullptr;
};

using CommandHandler = int (*)(const std::vector<std::string>& args, AppState& state);
//Receives the arguments and the state of the process
//to return a situation number

//Indicates the structure of a command
struct Command {
    std::string name;
    std::string usage;
    std::string description;
    CommandHandler handler;
};

//Indicates the structure of what a user
//can write down and saves it this way
struct ParsedCommand {
    std::string name;
    std::vector<std::string> args;
};

//List of the commands available
inline std::vector<Command> commands;

//List of commands defined in other fiels
ParsedCommand parse_command(const std::string& line);

bool execute_command(const ParsedCommand& command, AppState& state);

int cmd_help(const std::vector<std::string>& args, AppState& state);
int cmd_clear(const std::vector<std::string>& args, AppState& state);
int cmd_exit(const std::vector<std::string>& args, AppState& state);

// Play
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

//Add path from arguments verifying everything
std::optional<playlist::TrackId> add_path_from_args(const std::vector<std::string>& args,
                                                    AppState& state);

void register_basic_commands();
void register_audio_commands();
void register_playlist_commands();
