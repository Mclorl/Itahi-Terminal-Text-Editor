#include "editor/undo_history.hpp"

#include "editor/workspace.hpp"

CommandResult undo_edit() {
    Document* document = workspace.active();
    if (!document) {
        return {false, "No file is open"};
    }
    if (!document->undo()) {
        return {false, "Nothing to undo"};
    }
    return {true, "Undo"};
}

CommandResult redo_edit() {
    Document* document = workspace.active();
    if (!document) {
        return {false, "No file is open"};
    }
    if (!document->redo()) {
        return {false, "Nothing to redo"};
    }
    return {true, "Redo"};
}
