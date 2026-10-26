#pragma once

// the modes that are available are: Normal | Edit | Command

// central place to switch modes, so the three flags can never disagree.
void set_normal_mode();
void set_insert_mode();
void set_command_mode();
