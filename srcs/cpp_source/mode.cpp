#include "../library/mode.hpp"

#include "../library/global_variables.hpp"

void set_normal_mode()  { is_normal_mode = true;  is_edit_mode = false; is_command_mode = false; }
void set_insert_mode()  { is_normal_mode = false; is_edit_mode = true;  is_command_mode = false; }
void set_command_mode() { is_normal_mode = false; is_edit_mode = false; is_command_mode = true;  }
