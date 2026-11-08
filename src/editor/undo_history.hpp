#pragma once

#include "editor/commands.hpp"

// undo / redo of the open file: Ctrl+Z / Ctrl+Y, "u" / Ctrl+R in Normal
// mode, >undo / >redo and the Edit tab. The steps themselves are kept by each
// Document (editor/document.hpp): typing a word is one step, a command or a
// paste is a step of its own.

CommandResult undo_edit();
CommandResult redo_edit();
