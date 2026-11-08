#include "editor/workspace.hpp"

#include <optional>
#include <system_error>

#include "core/file_io.hpp"
#include "editor/state.hpp"

namespace fs = std::filesystem;

bool open_file(const fs::path& p, std::string& content_main_user_updated, std::string& active_file_local,
           std::string& long_active_file_name_local) {
    const std::string key = p.string();

    // read on first open, not when the folder is listed.
    std::error_code exists_error;
    if (!content_already_read[key] && !fs::exists(p, exists_error) && !exists_error) {
        // a new file: an empty buffer, created on disk by the first save.
        content_read_first[key].clear();
        content_already_read[key] = true;
        content_main_user_updated.clear();
    } else if (!content_already_read[key]) {
        std::string error;
        std::optional<std::string> text = read_text_file(p, error);
        if (!text) {
            command_error = error;
            return false;
        }
        content_read_first[key] = std::move(*text);
        remember_disk_stamp(key);
        content_already_read[key] = true;
        content_main_user_updated = content_read_first[key];
    } else if (file_unsaved_status[key]) {
        content_main_user_updated = content_user_updated[key];
    } else {
        content_main_user_updated = content_read_first[key];
    }

    // the cursor belongs to the file.
    file_cursor[long_active_file_name_local] = cursor_index;
    cursor_index = file_cursor[key];

    const std::string file_name = p.filename().string();
    active_file_not_saved = file_name + "*";
    active_file_saved = file_name;
    long_active_file_name_local = key;
    active_file_local = file_unsaved_status[key] ? active_file_not_saved : active_file_saved;
    return true;
}
