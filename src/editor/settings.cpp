#include "editor/settings.hpp"

#include <algorithm>
#include <exception>
#include <string>

#include "editor/state.hpp"

// mouse sensitivity input fallback function (1 - 99, default 1).
int get_mouse_sensitivity() {
    try {
        return mouse_sensitivity.empty() ? 1 : std::max(1, std::stoi(mouse_sensitivity));
    } catch (const std::exception&) {
        return 1;
    }
}

// tab size input fallback function (1 - 16, default 4).
int get_tab_size() {
    try {
        const int size = tab_size.empty() ? 4 : std::stoi(tab_size);
        return size < 1 ? 4 : std::min(size, 16);
    } catch (const std::exception&) {
        return 4;
    }
}

