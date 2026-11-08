#pragma once

#include <string>
#include <string_view>

// the system clipboard. Copying always keeps the text in Itahi too, and also
// gives it to the system clipboard: through the terminal (the OSC 52
// sequence, which works over SSH in terminals that support it) and through
// wl-copy (Wayland) or xclip / xsel (X11) when they are installed.

// copies `text`. Returns where it went, for the status message.
std::string clipboard_copy(const std::string& text);

// the text to paste: the system clipboard when wl-paste / xclip / xsel can
// read it, else the last text copied in Itahi. `from_system` says which.
std::string clipboard_paste(bool& from_system);

// the OSC 52 sequence that asks the terminal to put `text` on the clipboard.
std::string osc52_sequence(std::string_view text);

std::string base64_encode(std::string_view data);
