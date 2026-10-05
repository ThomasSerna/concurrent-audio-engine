#pragma once

#include <string>
#include <vector>

struct AppState {

    std::vector<std::string> console_lines;

    bool should_exit = false;
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

int cmd_play(const std::vector<std::string>& args, AppState& state);
int cmd_stop(const std::vector<std::string>& args, AppState& state);
int cmd_resume(const std::vector<std::string>& args, AppState& state);
int cmd_pause(const std::vector<std::string>& args, AppState& state);
int cmd_close();

void register_basic_commands();
void register_audio_commands();