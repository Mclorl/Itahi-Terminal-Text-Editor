# Itahi Terminal Text Editor

A Terminal User Interface (TUI) text editor that has the familiar UI and inputs you see in most Graphical User Interface (GUI) text editors, plus vim-like modes for working from the keyboard. Made with C++ and [FTXUI](https://github.com/ArthurSonzogni/FTXUI).

> **Work in progress.** Some menu entries (New File, Undo, Redo) are placeholders for now.

## Features

- **File explorer** on the left, with folders that open and close, a resizable width (drag the `⋮` separator) and markers for unsaved files.
- **Menu bar** with File, Edit, View, Settings and Help tabs, usable with the mouse or the keyboard.
- **Modes like vim:** Normal, Edit and Command mode, shown on the right of the top bar.
- **Commands:** `w` to save, `q` to quit, `wq` to save and quit, `q!` to quit without saving.
- **Syntax highlighting** for about 50 languages (C, C++, Python, JavaScript, TypeScript, Rust, Go, Java, CMake, Markdown, ...) powered by [tree-sitter](https://github.com/tree-sitter/tree-sitter), with colors you can change in the View tab.
- **Line numbers**, mouse wheel scrolling, an adjustable mouse sensitivity and tab size.
- **Binary file safety:** binary files (compiled programs, `.a` libraries, ...) and files that aren't UTF-8 are not shown until you choose **Open Anyway**, so they can't break the editor's display.

## Requirements

- Linux (developed and tested on Fedora)
- [CMake](https://cmake.org/) 3.14 or newer
- A C++17 compiler (GCC or Clang) and a C compiler
- Git
- An internet connection for the first build: CMake downloads FTXUI, tree-sitter and the language grammars automatically.

On Fedora:

```sh
sudo dnf install cmake gcc gcc-c++ git
```

On Debian / Ubuntu:

```sh
sudo apt install cmake build-essential git
```

## Installation

1. Clone the repository:

   ```sh
   git clone https://github.com/Mclorl/Itahi-Terminal-Text-Editor.git
   cd Itahi-Terminal-Text-Editor
   ```

2. Configure the project. This downloads the dependencies, so the first run takes a while:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   ```

3. Build it:

   ```sh
   cmake --build build -j
   ```

   The editor is built as `build/main`.

4. *(Optional)* Put it on your `PATH` as `itahi` so you can start it from any folder:

   ```sh
   sudo install -m 755 build/main /usr/local/bin/itahi
   ```

To update later, run `git pull` and step 3 again.

## Usage

```sh
./build/main                 # open the current folder in the explorer
./build/main path/to/folder  # open a folder
./build/main path/to/file    # open a single file (no explorer)
```

(Use `itahi` instead of `./build/main` if you did step 4.)

The editor starts in **Normal mode**. Select a file in the explorer to open it.

### Keys

| Key | Mode | Action |
| --- | --- | --- |
| `e` | Normal | Enter Edit mode (type in the file) |
| `Esc` | Edit | Back to Normal mode |
| `h` `j` `k` `l` / arrows | Normal | Move the cursor |
| `Ctrl` + arrows, or `Ctrl` + `H` `J` `K` `L` | Normal | Move between the top bar, the explorer and the editor |
| `Tab` / `Shift+Tab` | Edit | Indent / unindent |
| `>` | Normal | Open the command line at the bottom of the editor |
| `Ctrl+C` | Any | Quit right away (unsaved changes are lost) |

### Commands

Press `>` and type a command, then `Enter`. `Esc` (or deleting the `>`) closes the command line.

| Command | Action |
| --- | --- |
| `w` | Save the open file |
| `q` | Quit (refused while a file has unsaved changes) |
| `wq` | Save, then quit |
| `q!` | Quit without saving |

An invalid command, or one that can't run, shows an error in red above the command line.

## Built with

- [FTXUI](https://github.com/ArthurSonzogni/FTXUI): the terminal UI library (patched at build time so `Ctrl+H` and `Ctrl+J` are separate from `Backspace` and `Enter`, see `patches/`)
- [tree-sitter](https://github.com/tree-sitter/tree-sitter) and its language grammars: syntax highlighting

## License

[GNU General Public License v3.0](LICENSE)
