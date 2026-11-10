#include "editor/state.hpp"

// command mode line
std::string command_line;
int command_cursor_index = 0;
std::string command_output;
std::string command_error;
std::vector<std::string> command_completions;
std::vector<std::string> command_history;

Mode mode = Mode::Normal;

Hooks hooks;

void remember_command(const std::string& command) {
    constexpr std::size_t most = 200;
    if (command.empty() || (!command_history.empty() && command_history.back() == command)) {
        return;  // nothing, or the same command again
    }
    command_history.push_back(command);
    if (command_history.size() > most) {
        command_history.erase(command_history.begin());
    }
}
