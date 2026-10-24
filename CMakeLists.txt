cmake_minimum_required(VERSION 3.14)
project(ItahiTextEditor VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
 
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG v7.0.2  # Replace with a version, tag, or commit hash
  # keep Ctrl+H / Ctrl+J distinct from Backspace / Enter (see the patch file).
  # skipped when the patch is already applied, so re-configuring is safe.
  PATCH_COMMAND sh -c "git apply --reverse --check '${CMAKE_CURRENT_SOURCE_DIR}/patches/ftxui-ctrl-h-ctrl-j.patch' 2>/dev/null || git apply '${CMAKE_CURRENT_SOURCE_DIR}/patches/ftxui-ctrl-h-ctrl-j.patch'"
  UPDATE_DISCONNECTED TRUE  # don't re-checkout the tag over the patched sources
)
 
FetchContent_MakeAvailable(ftxui)

# syntax highlighting (tree-sitter + grammars), defines ITAHI_GENERATED_DIR.
include(cmake/syntax_languages.cmake)

add_library(my_library # libraries that needed to be compiled.
  srcs/cpp_source/app_data.cpp
  srcs/cpp_source/syntax_highlight.cpp
  ${ITAHI_GENERATED_DIR}/syntax_languages.cpp
  srcs/cpp_source/custom_styling.cpp
  srcs/cpp_source/file_entry.cpp
  srcs/cpp_source/file_system_left_panel.cpp
  srcs/cpp_source/functions.cpp
  srcs/cpp_source/tab_contents.cpp
  srcs/cpp_source/global_variables.cpp
  srcs/cpp_source/mode.cpp
  srcs/cpp_source/terminal_handling.cpp


  # headers
  srcs/library/app_data.hpp
  srcs/library/file_system_left_panel.hpp
  srcs/library/file_entry.hpp
  srcs/library/custom_styling.hpp
  srcs/library/tab_contents.hpp
  srcs/library/global_variables.hpp
  srcs/library/sets_of_tools.hpp
  srcs/library/functions.hpp
  srcs/library/terminal_handling.hpp
  srcs/library/mode.hpp
  srcs/library/syntax_highlight.hpp
  srcs/library/syntax_languages.hpp
)

# the generated language table includes "syntax_languages.hpp".
target_include_directories(my_library
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/srcs/library
)
 
add_executable(main  # executable only one for now.
  main.cpp
)

target_include_directories(main
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/srcs/library
)

target_link_libraries(my_library
  PUBLIC
    ftxui::screen
    ftxui::dom
    ftxui::component
    itahi_grammars
)

target_link_libraries(main
  PRIVATE
    my_library
)