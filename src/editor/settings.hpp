#pragma once

// the numbers typed in the Settings tab, with safe fallbacks.

// mouse wheel: rows per notch (1 - 99, default 1 when empty or invalid).
int get_mouse_sensitivity();

// spaces per indent level (1 - 16, default 4).
int get_tab_size();
