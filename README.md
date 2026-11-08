# Itahi Terminal Text Editor

A Terminal User Interface (TUI) text editor that has the familiar UI and inputs you see in most Graphical User Interface (GUI) text editors, plus vim-like modes for working from the keyboard. Made with C++ and [FTXUI](https://github.com/ArthurSonzogni/FTXUI).

> **Work in progress.** Tab characters are shown without indentation for now, and clicking in a word-wrapped line doesn't place the cursor yet.

## Features

- **File explorer** on the left, with folders that open and close, a resizable width (drag the `⋮` separator) and markers for unsaved files.
- **Menu bar** with File, Edit, View, Settings and Help tabs, usable with the mouse or the keyboard.
- **Modes like vim:** Normal, Edit and Command mode, shown on the right of the top bar.
- **Commands like vim:** save, quit, open, save as, undo, redo, search, substitute and go to line (see below).
- **Undo and redo** with `Ctrl+Z` / `Ctrl+Y`, or `u` / `Ctrl+R` in Normal mode.
- **Word wrap** (View tab).
- **Safe saving:** files are written to a temporary file and renamed into place, so a failed save never damages the original. Changes made to a file outside the editor are detected before saving over them.
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
   cmake -S . -B build
   ```

   This builds an optimised (Release) editor unless you choose another build type. To bundle only some languages (a much smaller binary), add for example `-DITAHI_LANGUAGES="C;C++;Python;JSON;Markdown"`.

3. Build it:

   ```sh
   cmake --build build -j
   ```

   The editor is built as `build/itahi`. Run the tests with `ctest --test-dir build`.

4. *(Optional)* Install it on your `PATH` so you can start it from any folder:

   ```sh
   sudo cmake --install build
   ```

To update later, run `git pull` and step 3 again.

## Usage

```sh
./build/itahi                 # open the current folder in the explorer
./build/itahi path/to/folder  # open a folder
./build/itahi path/to/file    # open a single file (no explorer); a new file is created by >w
```

(Use `itahi` instead of `./build/itahi` if you did step 4.)

The editor starts in **Normal mode**. Select a file in the explorer to open it.

### Keys

| Key | Mode | Action |
| --- | --- | --- |
| `e` or `i` | Normal | Enter Edit mode (type in the file) |
| `Esc` | Edit | Back to Normal mode (works wherever the focus is) |
| `h` `j` `k` `l` / arrows | Normal | Move the cursor |
| `Home` / `End`, `Page Up` / `Page Down` | Any | Start / end of the line, one screen up / down |
| `/` or `?` | Normal | Find, like vscode: every match is highlighted as you type; `Enter` (or `Down`) goes to the next match, again and again; `Up` goes back; `Esc` closes |
| `n` / `N` | Normal | Next / previous match of the last search |
| `u` / `Ctrl+R` | Normal | Undo / redo |
| `Ctrl+Z` / `Ctrl+Y` | Normal, Edit | Undo / redo |
| `Ctrl+S` | Normal, Edit | Save |
| `Ctrl` + arrows, or `Ctrl` + `H` `J` `K` `L` | Normal | Move between the top bar, the explorer and the editor |
| `Tab` / `Shift+Tab` | Edit | Indent / unindent |
| `>` | Normal | Open the command line at the bottom of the editor |
| `Ctrl+C` | Any | Quit (refused while a file has unsaved changes) |

### Commands

Press `>` and type a command, then `Enter`. `Esc` (or deleting the `>`) closes the command line.

| Command | Action |
| --- | --- |
| `w` | Save the open file (refused if the file changed on disk since it was opened) |
| `w!` | Save even if the file changed on disk |
| `w name` | Save as `name`, creating missing folders (`w! name` to overwrite an existing file) |
| `wa` | Save every unsaved file |
| `q` | Quit (refused while a file has unsaved changes) |
| `wq` | Save, then quit |
| `q!` | Quit without saving |
| `e name` | Open `name`. A file that doesn't exist yet is created right away, with any missing folders, and shown in the explorer |
| `u`, `undo` / `red`, `redo` | Undo / redo |
| `/text`, `?text` | Search down / up for the exact text (see `/` above) |
| `s/old/new/` | Replace on the cursor's line (`g` at the end: every match on the line) |
| `%s/old/new/g` | Replace in the whole file |
| `42` | Go to line 42 |

A path starting with `/` is inside the folder Itahi opened (`>e /test/newFile.txt` creates `test/newFile.txt` there), `~/` is your home folder, anything else is relative. After `>e ` or `>w `, `Tab` completes folder and file names like a shell's `cd`; when several match they are listed under the command line.

**New File** (File tab) opens `>e` in the folder selected in the explorer (or the folder of the selected file): type the name and press `Enter`.

An invalid command, or one that can't run, shows an error in red above the command line. Pressing command keys in Edit mode while the explorer or a menu has the focus shows a reminder to press `Esc` first.

## Built with

- [FTXUI](https://github.com/ArthurSonzogni/FTXUI): the terminal UI library (patched at build time so `Ctrl+H` and `Ctrl+J` are separate from `Backspace` and `Enter`, see `patches/`)
- [tree-sitter](https://github.com/tree-sitter/tree-sitter) and its language grammars: syntax highlighting

## License

[GNU General Public License v3.0](LICENSE)
