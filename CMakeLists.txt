cmake_minimum_required(VERSION 3.14)
project(ItahiTextEditor VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Without this a plain "cmake -S . -B build" builds with no optimisation at all
# (typing is ~5-10x slower on big files).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ITAHI_LANGUAGES "all" CACHE STRING
    "Languages to bundle: 'all' or a list such as C;C++;Python")
option(ITAHI_WARNINGS_AS_ERRORS "Treat warnings in our own code as errors" OFF)
option(ITAHI_BUILD_TESTS "Build the unit tests (downloads doctest)" ON)

include(FetchContent)
include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

# syntax highlighting (tree-sitter + grammars), defines ITAHI_GENERATED_DIR.
include(cmake/SyntaxLanguages.cmake)

# everything except main(): also linked by the tests. Folders are layers:
# core <- editor, syntax <- ui <- main (see documentary/).
add_library(itahi_core
  src/core/encoding.cpp
  src/core/file_io.cpp
  src/editor/commands.cpp
  src/editor/config.cpp
  src/editor/document.cpp
  src/editor/mode.cpp
  src/editor/motion.cpp
  src/editor/paths.cpp
  src/editor/search.cpp
  src/editor/settings.cpp
  src/editor/state.cpp
  src/editor/undo_history.cpp
  src/editor/workspace.cpp
  src/syntax/highlighter.cpp
  src/syntax/predicates.cpp
  src/syntax/theme.cpp
  ${ITAHI_GENERATED_DIR}/syntax_languages.cpp
  src/ui/app_view.cpp
  src/ui/command_line.cpp
  src/ui/editor_view.cpp
  src/ui/explorer.cpp
  src/ui/glyphs.cpp
  src/ui/key_listener.cpp
  src/ui/key_router.cpp
  src/ui/menu_bar.cpp
  src/ui/menu_pages.cpp
  src/ui/status_bar.cpp
  src/ui/tab_bar.cpp
  src/ui/ui_state.cpp
  src/ui/widgets.cpp
  src/platform/clipboard.cpp
  src/platform/git.cpp
  src/platform/open_url.cpp
  src/platform/process.cpp
  src/platform/terminal.cpp
)

target_include_directories(itahi_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(itahi_core PUBLIC ITAHI_VERSION_STRING="${PROJECT_VERSION}")

find_package(Threads REQUIRED)
target_link_libraries(itahi_core
  PUBLIC
    ftxui::screen
    ftxui::dom
    ftxui::component
    itahi_grammars
    Threads::Threads
)

add_executable(itahi src/main.cpp)
target_link_libraries(itahi PRIVATE itahi_core)

itahi_set_warnings(itahi_core)
itahi_set_warnings(itahi)

include(GNUInstallDirs)
install(TARGETS itahi RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(ITAHI_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest
    GIT_TAG v2.4.12
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(doctest)

  enable_testing()
  add_executable(itahi_tests
    tests/main.cpp
    tests/core/test_encoding.cpp
    tests/core/test_file_io.cpp
    tests/editor/test_commands.cpp
    tests/editor/test_config.cpp
    tests/editor/test_document.cpp
    tests/editor/test_paths.cpp
    tests/editor/test_workspace.cpp
    tests/platform/test_clipboard.cpp
    tests/platform/test_git.cpp
    tests/ui/test_explorer.cpp
    tests/ui/test_glyphs.cpp
  )
  target_include_directories(itahi_tests PRIVATE tests)
  target_link_libraries(itahi_tests PRIVATE itahi_core doctest::doctest)
  add_test(NAME itahi_tests COMMAND itahi_tests)
endif()
