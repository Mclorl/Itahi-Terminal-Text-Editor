# Syntax highlighting: tree-sitter core + one grammar per language.
#
# Every grammar is downloaded (not built with its own CMakeLists), its
# parser.c / scanner.c are compiled into `itahi_grammars`, and its
# highlights.scm query files are embedded into a generated C++ table
# (`syntax_languages[]`, see srcs/library/syntax_languages.hpp).
#
# Adding a language = one itahi_fetch_grammar() + one itahi_add_language().

# a SOURCE_SUBDIR without a CMakeLists.txt makes FetchContent only download.
set(ITAHI_NO_SUBDIR itahi-download-only)

# --- tree-sitter core ---
FetchContent_Declare(tree_sitter
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter
  GIT_TAG v0.27.1
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR ${ITAHI_NO_SUBDIR}
)
FetchContent_MakeAvailable(tree_sitter)

add_library(tree_sitter_core STATIC ${tree_sitter_SOURCE_DIR}/lib/src/lib.c)
target_include_directories(tree_sitter_core
  PRIVATE ${tree_sitter_SOURCE_DIR}/lib/src
  PUBLIC  ${tree_sitter_SOURCE_DIR}/lib/include
)
target_compile_definitions(tree_sitter_core PRIVATE _POSIX_C_SOURCE=200112L _DEFAULT_SOURCE)
set_target_properties(tree_sitter_core PROPERTIES C_STANDARD 11)
# always optimized (even in Debug builds): the parser runs on every keystroke.
target_compile_options(tree_sitter_core PRIVATE -O2)

# --- grammar helpers ---
set(ITAHI_GRAMMAR_SOURCES "")
set(ITAHI_LANGUAGE_DECLS "")
set(ITAHI_LANGUAGE_TABLE "")

# itahi_fetch_grammar(<id> <github owner/repo> <tag or commit>) -> ${<id>_SOURCE_DIR}
# a full commit hash (for grammars without a recent release tag) is fetched as
# a source archive, since a shallow git clone can only fetch tags/branches.
macro(itahi_fetch_grammar id repo ref)
  if("${ref}" MATCHES "^[0-9a-f]+$" AND NOT "${ref}" MATCHES "^[0-9]+$")
    FetchContent_Declare(${id}
      URL https://github.com/${repo}/archive/${ref}.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      SOURCE_SUBDIR ${ITAHI_NO_SUBDIR}
    )
  else()
    FetchContent_Declare(${id}
      GIT_REPOSITORY https://github.com/${repo}
      GIT_TAG ${ref}
      GIT_SHALLOW TRUE
      GIT_SUBMODULES ""  # none needed (and tree-sitter-cuda's .gitmodules is broken)
      SOURCE_SUBDIR ${ITAHI_NO_SUBDIR}
    )
  endif()
  FetchContent_MakeAvailable(${id})
endmacro()

# itahi_add_language(NAME <display name> SYMBOL <tree_sitter_xxx> SRC <grammar src dir>
#                    MATCH <extensions / file names...> QUERIES <highlights.scm files...>)
# QUERIES are concatenated in order; earlier patterns win (tree-sitter rule).
function(itahi_add_language)
  cmake_parse_arguments(L "" "NAME;SYMBOL;SRC" "MATCH;QUERIES" ${ARGN})

  set(sources ${L_SRC}/parser.c)
  if(EXISTS ${L_SRC}/scanner.c)
    list(APPEND sources ${L_SRC}/scanner.c)
  endif()
  # each grammar finds its own "tree_sitter/parser.h" (shared headers such as
  # typescript's common/scanner.h include it by name, not by relative path).
  set_source_files_properties(${sources} PROPERTIES INCLUDE_DIRECTORIES ${L_SRC})

  set(query "")
  foreach(query_file ${L_QUERIES})
    file(READ ${query_file} query_text)
    string(APPEND query "${query_text}\n")
  endforeach()

  # stored as " c h " so a lookup can search for " <ext> ".
  list(JOIN L_MATCH " " match)

  set(ITAHI_GRAMMAR_SOURCES ${ITAHI_GRAMMAR_SOURCES} ${sources} PARENT_SCOPE)
  set(ITAHI_LANGUAGE_DECLS "${ITAHI_LANGUAGE_DECLS}extern \"C\" const TSLanguage* ${L_SYMBOL}(void);\n" PARENT_SCOPE)
  set(ITAHI_LANGUAGE_TABLE "${ITAHI_LANGUAGE_TABLE}    {\"${L_NAME}\", ${L_SYMBOL}, \" ${match} \", R\"itahi_query(${query})itahi_query\"},\n" PARENT_SCOPE)
endfunction()

# --- languages ---
itahi_fetch_grammar(ts_c          tree-sitter/tree-sitter-c          v0.24.2)
itahi_fetch_grammar(ts_cpp        tree-sitter/tree-sitter-cpp        v0.23.4)
itahi_fetch_grammar(ts_python     tree-sitter/tree-sitter-python     v0.25.0)
itahi_fetch_grammar(ts_javascript tree-sitter/tree-sitter-javascript v0.25.0)
itahi_fetch_grammar(ts_typescript tree-sitter/tree-sitter-typescript v0.23.2)
itahi_fetch_grammar(ts_rust       tree-sitter/tree-sitter-rust       v0.24.2)
itahi_fetch_grammar(ts_go         tree-sitter/tree-sitter-go         v0.25.0)
itahi_fetch_grammar(ts_java       tree-sitter/tree-sitter-java       v0.23.5)
itahi_fetch_grammar(ts_bash       tree-sitter/tree-sitter-bash       v0.25.1)
itahi_fetch_grammar(ts_json       tree-sitter/tree-sitter-json       v0.24.8)
itahi_fetch_grammar(ts_html       tree-sitter/tree-sitter-html       v0.23.2)
itahi_fetch_grammar(ts_css        tree-sitter/tree-sitter-css        v0.25.0)
itahi_fetch_grammar(ts_lua        tree-sitter-grammars/tree-sitter-lua v0.5.0)
itahi_fetch_grammar(ts_ruby       tree-sitter/tree-sitter-ruby       v0.23.1)
itahi_fetch_grammar(ts_csharp     tree-sitter/tree-sitter-c-sharp    v0.23.5)
itahi_fetch_grammar(ts_php        tree-sitter/tree-sitter-php        v0.25.1)
itahi_fetch_grammar(ts_haskell    tree-sitter/tree-sitter-haskell    v0.24.1)
itahi_fetch_grammar(ts_ocaml      tree-sitter/tree-sitter-ocaml      v0.26.0)
itahi_fetch_grammar(ts_scala      tree-sitter/tree-sitter-scala      v0.26.3)
itahi_fetch_grammar(ts_julia      tree-sitter/tree-sitter-julia      v0.25.0)
itahi_fetch_grammar(ts_embedded_template tree-sitter/tree-sitter-embedded-template v0.25.0)
itahi_fetch_grammar(ts_markdown   tree-sitter-grammars/tree-sitter-markdown v0.5.4)
itahi_fetch_grammar(ts_yaml       tree-sitter-grammars/tree-sitter-yaml     v0.7.2)
itahi_fetch_grammar(ts_toml       tree-sitter-grammars/tree-sitter-toml     v0.7.0)
itahi_fetch_grammar(ts_xml        tree-sitter-grammars/tree-sitter-xml      v0.7.0)
itahi_fetch_grammar(ts_make       tree-sitter-grammars/tree-sitter-make     v1.1.1)
itahi_fetch_grammar(ts_zig        tree-sitter-grammars/tree-sitter-zig      v1.1.2)
itahi_fetch_grammar(ts_vim        tree-sitter-grammars/tree-sitter-vim      v0.8.1)
itahi_fetch_grammar(ts_glsl       tree-sitter-grammars/tree-sitter-glsl     v0.2.0)
itahi_fetch_grammar(ts_objc       tree-sitter-grammars/tree-sitter-objc     v3.0.2)
itahi_fetch_grammar(ts_scss       tree-sitter-grammars/tree-sitter-scss     v1.0.0)
itahi_fetch_grammar(ts_diff       tree-sitter-grammars/tree-sitter-diff     v0.2.0)
itahi_fetch_grammar(ts_cuda       tree-sitter-grammars/tree-sitter-cuda     v0.21.2)
itahi_fetch_grammar(ts_odin       tree-sitter-grammars/tree-sitter-odin     v1.3.0)
itahi_fetch_grammar(ts_kotlin     fwcd/tree-sitter-kotlin            1852ea17b7f60fb3f9d84e0b1555d56b46b39fb1)
itahi_fetch_grammar(ts_cmake      uyha/tree-sitter-cmake             v0.7.5)
itahi_fetch_grammar(ts_dockerfile camdencheek/tree-sitter-dockerfile v0.2.0)
itahi_fetch_grammar(ts_elixir     elixir-lang/tree-sitter-elixir     v0.3.5)
itahi_fetch_grammar(ts_erlang     WhatsApp/tree-sitter-erlang        62c82e67b73e13f3ef06e4c8c07ff035a8235d30)
itahi_fetch_grammar(ts_nix        nix-community/tree-sitter-nix      v0.3.0)
itahi_fetch_grammar(ts_r          r-lib/tree-sitter-r                v1.3.0)
itahi_fetch_grammar(ts_dart       UserNobody14/tree-sitter-dart      0038280b774b65c1b89ba7fec50624d8baed9fe1)
itahi_fetch_grammar(ts_powershell airbus-cert/tree-sitter-powershell v0.26.5)
itahi_fetch_grammar(ts_fortran    stadelmanma/tree-sitter-fortran    v0.6.0)
itahi_fetch_grammar(ts_elm        elm-tooling/tree-sitter-elm        v5.9.4)
itahi_fetch_grammar(ts_graphql    bkegley/tree-sitter-graphql        5e66e961eee421786bdda8495ed1db045e06b5fe)
itahi_fetch_grammar(ts_awk        Beaglefoot/tree-sitter-awk         v0.7.2)

# query lists follow each grammar's tree-sitter.json ("highlights").
itahi_add_language(NAME C SYMBOL tree_sitter_c SRC ${ts_c_SOURCE_DIR}/src
  MATCH c
  QUERIES ${ts_c_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME C++ SYMBOL tree_sitter_cpp SRC ${ts_cpp_SOURCE_DIR}/src
  MATCH cpp cc cxx c++ h hpp hh hxx h++ ino
  QUERIES ${ts_c_SOURCE_DIR}/queries/highlights.scm ${ts_cpp_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Python SYMBOL tree_sitter_python SRC ${ts_python_SOURCE_DIR}/src
  MATCH py pyw pyi
  QUERIES ${ts_python_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME JavaScript SYMBOL tree_sitter_javascript SRC ${ts_javascript_SOURCE_DIR}/src
  MATCH js mjs cjs jsx
  QUERIES ${ts_javascript_SOURCE_DIR}/queries/highlights.scm
          ${ts_javascript_SOURCE_DIR}/queries/highlights-jsx.scm
          ${ts_javascript_SOURCE_DIR}/queries/highlights-params.scm)

itahi_add_language(NAME TypeScript SYMBOL tree_sitter_typescript SRC ${ts_typescript_SOURCE_DIR}/typescript/src
  MATCH ts mts cts
  QUERIES ${ts_typescript_SOURCE_DIR}/queries/highlights.scm
          ${ts_javascript_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME TSX SYMBOL tree_sitter_tsx SRC ${ts_typescript_SOURCE_DIR}/tsx/src
  MATCH tsx
  QUERIES ${ts_typescript_SOURCE_DIR}/queries/highlights.scm
          ${ts_javascript_SOURCE_DIR}/queries/highlights-jsx.scm
          ${ts_javascript_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Rust SYMBOL tree_sitter_rust SRC ${ts_rust_SOURCE_DIR}/src
  MATCH rs
  QUERIES ${ts_rust_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Go SYMBOL tree_sitter_go SRC ${ts_go_SOURCE_DIR}/src
  MATCH go
  QUERIES ${ts_go_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Java SYMBOL tree_sitter_java SRC ${ts_java_SOURCE_DIR}/src
  MATCH java
  QUERIES ${ts_java_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Bash SYMBOL tree_sitter_bash SRC ${ts_bash_SOURCE_DIR}/src
  MATCH sh bash zsh .bashrc .bash_profile .profile .zshrc
  QUERIES ${ts_bash_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME JSON SYMBOL tree_sitter_json SRC ${ts_json_SOURCE_DIR}/src
  MATCH json
  QUERIES ${ts_json_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME HTML SYMBOL tree_sitter_html SRC ${ts_html_SOURCE_DIR}/src
  MATCH html htm
  QUERIES ${ts_html_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME CSS SYMBOL tree_sitter_css SRC ${ts_css_SOURCE_DIR}/src
  MATCH css
  QUERIES ${ts_css_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Lua SYMBOL tree_sitter_lua SRC ${ts_lua_SOURCE_DIR}/src
  MATCH lua
  QUERIES ${ts_lua_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Ruby SYMBOL tree_sitter_ruby SRC ${ts_ruby_SOURCE_DIR}/src
  MATCH rb rake gemspec Rakefile Gemfile
  QUERIES ${ts_ruby_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME "C#" SYMBOL tree_sitter_c_sharp SRC ${ts_csharp_SOURCE_DIR}/src
  MATCH cs csx
  QUERIES ${ts_csharp_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME PHP SYMBOL tree_sitter_php SRC ${ts_php_SOURCE_DIR}/php/src
  MATCH php phtml
  QUERIES ${ts_php_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Haskell SYMBOL tree_sitter_haskell SRC ${ts_haskell_SOURCE_DIR}/src
  MATCH hs hs-boot
  QUERIES ${ts_haskell_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME OCaml SYMBOL tree_sitter_ocaml SRC ${ts_ocaml_SOURCE_DIR}/grammars/ocaml/src
  MATCH ml
  QUERIES ${ts_ocaml_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME "OCaml Interface" SYMBOL tree_sitter_ocaml_interface SRC ${ts_ocaml_SOURCE_DIR}/grammars/interface/src
  MATCH mli
  QUERIES ${ts_ocaml_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Scala SYMBOL tree_sitter_scala SRC ${ts_scala_SOURCE_DIR}/src
  MATCH scala sbt sc
  QUERIES ${ts_scala_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Julia SYMBOL tree_sitter_julia SRC ${ts_julia_SOURCE_DIR}/src
  MATCH jl
  QUERIES ${ts_julia_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME "Embedded Template" SYMBOL tree_sitter_embedded_template SRC ${ts_embedded_template_SOURCE_DIR}/src
  MATCH erb ejs
  QUERIES ${ts_embedded_template_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Markdown SYMBOL tree_sitter_markdown SRC ${ts_markdown_SOURCE_DIR}/tree-sitter-markdown/src
  MATCH md markdown
  QUERIES ${ts_markdown_SOURCE_DIR}/tree-sitter-markdown/queries/highlights.scm)

itahi_add_language(NAME YAML SYMBOL tree_sitter_yaml SRC ${ts_yaml_SOURCE_DIR}/src
  MATCH yml yaml
  QUERIES ${ts_yaml_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME TOML SYMBOL tree_sitter_toml SRC ${ts_toml_SOURCE_DIR}/src
  MATCH toml Cargo.lock
  QUERIES ${ts_toml_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME XML SYMBOL tree_sitter_xml SRC ${ts_xml_SOURCE_DIR}/xml/src
  MATCH xml svg xsd xsl xslt rng plist
  QUERIES ${ts_xml_SOURCE_DIR}/queries/xml/highlights.scm)

itahi_add_language(NAME Make SYMBOL tree_sitter_make SRC ${ts_make_SOURCE_DIR}/src
  MATCH mk mak Makefile makefile GNUmakefile
  QUERIES ${ts_make_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Zig SYMBOL tree_sitter_zig SRC ${ts_zig_SOURCE_DIR}/src
  MATCH zig zon
  QUERIES ${ts_zig_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Vim SYMBOL tree_sitter_vim SRC ${ts_vim_SOURCE_DIR}/src
  MATCH vim .vimrc vimrc
  QUERIES ${ts_vim_SOURCE_DIR}/queries/vim/highlights.scm)

# glsl / objc / cuda queries start with "; inherits: c" (or cpp): prepend those.
itahi_add_language(NAME GLSL SYMBOL tree_sitter_glsl SRC ${ts_glsl_SOURCE_DIR}/src
  MATCH glsl vert frag geom comp tesc tese vsh fsh
  QUERIES ${ts_c_SOURCE_DIR}/queries/highlights.scm ${ts_glsl_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Objective-C SYMBOL tree_sitter_objc SRC ${ts_objc_SOURCE_DIR}/src
  MATCH m
  QUERIES ${ts_c_SOURCE_DIR}/queries/highlights.scm ${ts_objc_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME CUDA SYMBOL tree_sitter_cuda SRC ${ts_cuda_SOURCE_DIR}/src
  MATCH cu cuh
  QUERIES ${ts_c_SOURCE_DIR}/queries/highlights.scm ${ts_cpp_SOURCE_DIR}/queries/highlights.scm ${ts_cuda_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME SCSS SYMBOL tree_sitter_scss SRC ${ts_scss_SOURCE_DIR}/src
  MATCH scss
  QUERIES ${ts_scss_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Diff SYMBOL tree_sitter_diff SRC ${ts_diff_SOURCE_DIR}/src
  MATCH diff patch
  QUERIES ${ts_diff_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Odin SYMBOL tree_sitter_odin SRC ${ts_odin_SOURCE_DIR}/src
  MATCH odin
  QUERIES ${ts_odin_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Kotlin SYMBOL tree_sitter_kotlin SRC ${ts_kotlin_SOURCE_DIR}/src
  MATCH kt kts
  QUERIES ${ts_kotlin_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME CMake SYMBOL tree_sitter_cmake SRC ${ts_cmake_SOURCE_DIR}/src
  MATCH cmake CMakeLists.txt
  QUERIES ${ts_cmake_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Dockerfile SYMBOL tree_sitter_dockerfile SRC ${ts_dockerfile_SOURCE_DIR}/src
  MATCH dockerfile Dockerfile Containerfile
  QUERIES ${ts_dockerfile_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Elixir SYMBOL tree_sitter_elixir SRC ${ts_elixir_SOURCE_DIR}/src
  MATCH ex exs
  QUERIES ${ts_elixir_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Erlang SYMBOL tree_sitter_erlang SRC ${ts_erlang_SOURCE_DIR}/src
  MATCH erl hrl escript rebar.config
  QUERIES ${ts_erlang_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Nix SYMBOL tree_sitter_nix SRC ${ts_nix_SOURCE_DIR}/src
  MATCH nix
  QUERIES ${ts_nix_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME R SYMBOL tree_sitter_r SRC ${ts_r_SOURCE_DIR}/src
  MATCH r
  QUERIES ${ts_r_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Dart SYMBOL tree_sitter_dart SRC ${ts_dart_SOURCE_DIR}/src
  MATCH dart
  QUERIES ${ts_dart_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME PowerShell SYMBOL tree_sitter_powershell SRC ${ts_powershell_SOURCE_DIR}/src
  MATCH ps1 psm1 psd1
  QUERIES ${ts_powershell_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Fortran SYMBOL tree_sitter_fortran SRC ${ts_fortran_SOURCE_DIR}/src
  MATCH f90 f95 f03 f08
  QUERIES ${ts_fortran_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME Elm SYMBOL tree_sitter_elm SRC ${ts_elm_SOURCE_DIR}/src
  MATCH elm
  QUERIES ${ts_elm_SOURCE_DIR}/queries/highlights.scm)

itahi_add_language(NAME GraphQL SYMBOL tree_sitter_graphql SRC ${ts_graphql_SOURCE_DIR}/src
  MATCH graphql gql
  QUERIES ${ts_graphql_SOURCE_DIR}/queries/graphql/highlights.scm)

itahi_add_language(NAME AWK SYMBOL tree_sitter_awk SRC ${ts_awk_SOURCE_DIR}/src
  MATCH awk
  QUERIES ${ts_awk_SOURCE_DIR}/queries/highlights.scm)

# --- build the grammars and the generated language table ---
add_library(itahi_grammars STATIC ${ITAHI_GRAMMAR_SOURCES})
set_target_properties(itahi_grammars PROPERTIES C_STANDARD 11)
target_link_libraries(itahi_grammars PUBLIC tree_sitter_core)
# generated grammar code: not ours to fix, keep the build output clean.
# -O2 like the core (the parse tables compile quickly either way).
target_compile_options(itahi_grammars PRIVATE -w -O2)

set(ITAHI_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(WRITE ${ITAHI_GENERATED_DIR}/syntax_languages.cpp.in
"// GENERATED by cmake/syntax_languages.cmake. Do not edit.
#include \"syntax_languages.hpp\"

${ITAHI_LANGUAGE_DECLS}
const SyntaxLanguage syntax_languages[] = {
${ITAHI_LANGUAGE_TABLE}};

const std::size_t syntax_language_count = sizeof(syntax_languages) / sizeof(syntax_languages[0]);
")
# only touch the real file when it changes, so re-configuring doesn't rebuild.
configure_file(${ITAHI_GENERATED_DIR}/syntax_languages.cpp.in
               ${ITAHI_GENERATED_DIR}/syntax_languages.cpp COPYONLY)
