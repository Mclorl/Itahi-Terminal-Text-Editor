#pragma once

// the modes that are available are: Normal | Edit | Command (see `Mode` in
// editor/state.hpp). Central place to switch modes.
void set_normal_mode();
void set_edit_mode();
void set_command_mode();
