# Itahi Terminal Text Editor

A Terminal User Interface (TUI) text editor that has the familiar UI and inputs you see in most Graphical User Interface (GUI) text editors, plus vim-like modes for working from the keyboard. Made with C++ and [FTXUI](https://github.com/ArthurSonzogni/FTXUI).

## Features

- **File explorer** on the left, with folders that open and close, a resizable width (drag the `⋮` separator), markers for unsaved files and, in a Git repository, each file's state (`M` modified, `U` new, `A` added, `D` deleted, `•` on a folder with changes; ignored files dimmed).
- **Tabs for open files** above the editor: click one to show it, click its `x` (or middle-click it) to close it. `Alt+Left` / `Alt+Right` (or `Ctrl+Page Up` / `Ctrl+Page Down`) switch between them.
- **Status bar** under the editor: the mode, the file (with `*` when unsaved), the Git branch and how many files changed, the selection, the line and column, the language, the indentation, the encoding and the line breaks (`LF` / `CRLF`).
- **Menu bar** with File, Edit, View, Settings and Help tabs, usable with the mouse or the keyboard.
- **Modes like vim:** Normal, Edit and Command mode, shown on the right of the top bar.
- **Commands like vim:** save, quit, open, save as, undo, redo, search, substitute and go to line (see below).
- **Undo and redo** with `Ctrl+Z` / `Ctrl+Y`, or `u` / `Ctrl+R` in Normal mode.
- **Selection and clipboard:** select with `Shift` + arrows, a mouse drag or `v` (Normal mode); `Ctrl+C` / `Ctrl+X` / `Ctrl+V` (or `y` / `d` / `p` in Normal mode) copy, cut and paste, also with the system clipboard (through the terminal, and through `wl-copy` / `xclip` / `xsel` when installed).
- **Word wrap** (View tab): long lines continue on the next row; `Up` / `Down` move by row on screen.
- **Tab characters** are shown as indentation up to the next tab stop (the Tab Size setting). The `Tab` key inserts spaces, or a tab character when *Tab Key Inserts Spaces* is off.
- **Fast with large files:** the editor draws only the rows on screen, so typing in a 100,000-line file stays quick.
- **Scroll style** (Settings tab): *Keep cursor centered* (default) keeps the cursor in the middle of the view and the mouse wheel moves the cursor; *Scroll freely* lets the wheel move only the view, so the cursor can go off screen until you move it again.
- **Safe saving:** files are written to a temporary file and renamed into place, so a failed save never damages the original. Changes made to a file outside the editor are detected before saving over them.
- **Syntax highlighting** for about 50 languages (C, C++, Python, JavaScript, TypeScript, Rust, Go, Java, CMake, Markdown, ...) powered by [tree-sitter](https://github.com/tree-sitter/tree-sitter), with colors you can change in the View tab.
- **Themes** (View tab): *Itahi*, *Ember*, *Glacier*, *Grove* and *Mono* set every syntax color and the editor's accents at once; each color can still be changed after.
- **Settings are kept:** every View and Settings choice is saved in `~/.config/itahi/config` (or `$XDG_CONFIG_HOME/itahi/config`) as soon as it changes.
- **Active row marking** (View tab): the cursor's row can be underlined in white, underlined in the text's color, or shaded with a background, which looks the same in every terminal (the default on the Linux console, which has no separate underline color).
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

| Key | Mode (from) | Action |
| --- | --- | --- |
| `e` or `i` | Normal | Enter Edit mode (type in the file) |
| `Esc` | Edit | Back to Normal mode (works wherever the focus is) |
| `h` `j` `k` `l` / arrows | Normal | Move the cursor |
| `Home` / `End`, `Page Up` / `Page Down` | Any | Start / end of the line, one screen up / down |
| `Ctrl+Home` / `Ctrl+End` | Any | Start / end of the file |
| `Ctrl+Left` / `Ctrl+Right` | Edit | Previous / next word |
| `Shift` + any of the moves above | Any | Select while moving |
| `v` | Normal | Start (or stop) selecting: the moves extend the selection |
| `y` / `d` or `x` / `p` | Normal | Copy / cut the selection, paste |
| `Ctrl+A` | Any | Select all |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Any | Copy / cut / paste |
| `Alt+Left` / `Alt+Right` | Normal, Edit | Previous / next open file (also `Ctrl+Page Up` / `Ctrl+Page Down`) |
| `F5` | Normal, Edit | Read the folders and the Git status again (after changes made by other programs) |
| `/` or `?` | Normal | Find, like vscode: every match is highlighted as you type; `Enter` (or `Down`) goes to the next match, again and again; `Up` goes back; `Esc` closes |
| `n` / `N` | Normal | Next / previous match of the last search |
| `u` / `Ctrl+R` | Normal | Undo / redo |
| `Ctrl+Z` / `Ctrl+Y` | Normal, Edit | Undo / redo |
| `Ctrl+S` | Normal, Edit | Save |
| `Ctrl+N` | Normal | New file in the active folder (see below) |
| `Ctrl` + arrows, or `Ctrl` + `H` `J` `K` `L` | Normal | Move between the top bar, the explorer and the editor |
| `Tab` / `Shift+Tab` | Edit | Indent / unindent |
| `>` | Normal | Open the command line at the bottom of the editor |
| `Ctrl+C` | Any | With text selected: copy it. Otherwise quit (refused while a file has unsaved changes) |

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
| `bn` / `bp` | Show the next / previous open file |
| `b name` | Show the open file called `name` (or the only one whose name starts with it) |
| `bd` | Close the open file (refused while it has unsaved changes; `bd!` closes it anyway) |
| `ls` | List the open files |
| `refresh` | Read the folders and the Git status again (like `F5`) |

The **active folder** is the folder focused in the explorer (mouse or keyboard), or the folder of the focused file; with nothing focused there, the folder of the open file. After `>e`, the active folder appears as a gray hint (eg. `>e docs/drafts/`): press `Tab` to use it, or just type a name to create the file in the folder Itahi opened instead. The end of the line shows where the file will be created. A path starting with `/` is inside the folder Itahi opened (`>e /test/newFile.txt` creates `test/newFile.txt` there), `~/` is your home folder, anything else is relative. After `>e ` or `>w `, `Tab` completes folder and file names like a shell's `cd`; when several match they are listed under the command line.

**New File** (File tab, or `Ctrl+N`) opens `>e` with the active folder already written in (eg. `>e src/`): type the name and press `Enter`.

An invalid command, or one that can't run, shows an error in red above the command line. Pressing command keys in Edit mode while the explorer or a menu has the focus shows a reminder to press `Esc` first.

### Settings file

The settings of the View and Settings tabs are saved in `~/.config/itahi/config` whenever one changes, one `name = value` per line:

```ini
tab_size = 4
word_wrap = true
scroll_style = free
theme = Ember
color.comment = Light Gray
```

The file can also be edited by hand; a line Itahi doesn't understand is reported when it starts and otherwise ignored.

## Built with

- [FTXUI](https://github.com/ArthurSonzogni/FTXUI): the terminal UI library (patched at build time so `Ctrl+H` and `Ctrl+J` are separate from `Backspace` and `Enter`, see `patches/`)
- [tree-sitter](https://github.com/tree-sitter/tree-sitter) and its language grammars: syntax highlighting

## License

[GNU General Public License v3.0](LICENSE)
