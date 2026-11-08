#include "editor/mode.hpp"

#include "editor/state.hpp"

void set_normal_mode()  { mode = Mode::Normal; }
void set_edit_mode()    { mode = Mode::Edit; }
void set_command_mode() { mode = Mode::Command; }
