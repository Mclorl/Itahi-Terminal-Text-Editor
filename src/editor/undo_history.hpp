#pragma once

#include "editor/commands.hpp"

// undo / redo of the open file. A snapshot of the buffer is taken before an
// edit, unless the previous edit was less than a moment ago: typing a word is
// one undo step.

// call before an edit made by typing.
void record_edit();

// call before an edit made by a command (>s, ...): always its own undo step.
void record_command_edit();

// Ctrl+Z / Ctrl+Y, "u" / Ctrl+R in Normal mode, >undo / >redo.
CommandResult undo_edit();
CommandResult redo_edit();
