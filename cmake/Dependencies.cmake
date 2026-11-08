# FTXUI, the terminal UI library. Patched so Ctrl+H / Ctrl+J stay distinct
# from Backspace / Enter (see patches/ftxui-ctrl-h-ctrl-j.patch).
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG v7.0.2
  # skipped when the patch is already applied, so re-configuring is safe.
  PATCH_COMMAND sh -c "git apply --reverse --check '${CMAKE_CURRENT_SOURCE_DIR}/patches/ftxui-ctrl-h-ctrl-j.patch' 2>/dev/null || git apply '${CMAKE_CURRENT_SOURCE_DIR}/patches/ftxui-ctrl-h-ctrl-j.patch'"
  UPDATE_DISCONNECTED TRUE  # don't re-checkout the tag over the patched sources
)

FetchContent_MakeAvailable(ftxui)
