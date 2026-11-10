"""Runs the real Itahi in a terminal emulator (pyte): sends keys and mouse
clicks, reads the screen back.

Safety: a scenario never deletes a folder it was *given*. Every work folder is
made here with tempfile.mkdtemp() and only those are removed afterwards (see
Lab and check_work_dir). The editor's config, state and trash folders are
redirected into the work folder too, so a run never touches the real ones.
"""

import fcntl
import os
import pty
import select
import shutil
import signal
import struct
import tempfile
import termios
import time

import pyte

LAB_PREFIX = "itahi-scenario-"

KEYS = {
    "up": b"\x1b[A", "down": b"\x1b[B", "right": b"\x1b[C", "left": b"\x1b[D",
    "home": b"\x1b[H", "end": b"\x1b[F", "pgup": b"\x1b[5~", "pgdn": b"\x1b[6~",
    "enter": b"\r", "esc": b"\x1b", "tab": b"\t", "shift-tab": b"\x1b[Z",
    "backspace": b"\x7f", "delete": b"\x1b[3~",
    "ctrl-up": b"\x1b[1;5A", "ctrl-down": b"\x1b[1;5B", "ctrl-right": b"\x1b[1;5C", "ctrl-left": b"\x1b[1;5D",
    "shift-up": b"\x1b[1;2A", "shift-down": b"\x1b[1;2B", "shift-right": b"\x1b[1;2C", "shift-left": b"\x1b[1;2D",
    "alt-right": b"\x1b[1;3C", "alt-left": b"\x1b[1;3D",
    "alt-shift-up": b"\x1b[1;4A", "alt-shift-down": b"\x1b[1;4B",
    "alt-shift-right": b"\x1b[1;4C", "alt-shift-left": b"\x1b[1;4D",
    "ctrl-end": b"\x1b[1;5F",
    "f1": b"\x1bOP", "f2": b"\x1bOQ", "f5": b"\x1b[15~", "f6": b"\x1b[17~", "shift-f6": b"\x1b[17;2~",
    "ctrl-c": b"\x03", "ctrl-n": b"\x0e", "ctrl-s": b"\x13", "ctrl-w": b"\x17", "ctrl-z": b"\x1a",
    "ctrl-backslash": b"\x1c", "alt-c": b"\x1bc",
}


def check_work_dir(path):
    """Refuses to touch anything that isn't a scenario folder made by Lab."""
    real = os.path.realpath(path)
    base = os.path.realpath(tempfile.gettempdir())
    name = os.path.basename(real)
    if os.path.dirname(real) != base or not name.startswith(LAB_PREFIX):
        raise SystemExit(f"refusing to use {path!r}: not a scenario folder under {base}")
    if os.path.exists(os.path.join(real, ".git")) or os.path.exists(os.path.join(real, "CMakeLists.txt")):
        raise SystemExit(f"refusing to use {path!r}: it looks like a project")


class Lab:
    """A fresh work folder for one scenario, removed afterwards. Always made
    here, never passed in."""

    def __init__(self):
        self.path = tempfile.mkdtemp(prefix=LAB_PREFIX)
        self.project = os.path.join(self.path, "project")
        self.home = os.path.join(self.path, "home")
        os.makedirs(self.project)
        os.makedirs(self.home)

    def write(self, name, text=""):
        path = os.path.join(self.project, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="") as out:
            out.write(text)
        return path

    def read(self, name):
        with open(os.path.join(self.project, name), encoding="utf-8", newline="") as source:
            return source.read()

    def exists(self, name):
        return os.path.exists(os.path.join(self.project, name))

    def env(self):
        env = dict(os.environ)
        env.update({
            "TERM": "xterm-256color",
            "HOME": self.home,
            "XDG_CONFIG_HOME": os.path.join(self.home, ".config"),
            "XDG_STATE_HOME": os.path.join(self.home, ".local", "state"),
            "XDG_DATA_HOME": os.path.join(self.home, ".local", "share"),
        })
        for name in ("WAYLAND_DISPLAY", "DISPLAY", "TMUX"):
            env.pop(name, None)  # no system clipboard in a scenario
        return env

    def remove(self):
        check_work_dir(self.path)
        shutil.rmtree(self.path, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.remove()


class _Screen(pyte.Screen):
    """Answers the terminal queries FTXUI sends at startup."""

    def write_process_input(self, data):
        try:
            os.write(self.fd, data.encode())
        except OSError:
            pass


class Editor:
    def __init__(self, exe, *args, lab, cols=120, rows=36):
        self.exit_code = None
        environment = lab.env()
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(lab.project)
            os.execve(exe, [exe, *args], environment)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.screen = _Screen(cols, rows)
        self.screen.fd = self.fd
        self.stream = pyte.ByteStream(self.screen)
        self.pump(0.5)

    def pump(self, quiet=0.12, limit=5.0):
        """Reads until the editor has been silent for `quiet` seconds."""
        deadline = time.time() + limit
        while time.time() < deadline and select.select([self.fd], [], [], quiet)[0]:
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                return
            if not data:
                return
            self.stream.feed(data)

    def wait_for(self, text, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end and not self.shows(text):
            self.pump(0.1, 0.3)
        return self.shows(text)

    @property
    def lines(self):
        return self.screen.display

    def shows(self, text):
        return any(text in line for line in self.lines)

    def find(self, text, nth=0):
        seen = 0
        for y, line in enumerate(self.lines):
            start = 0
            while (x := line.find(text, start)) >= 0:
                if seen == nth:
                    return x, y
                seen += 1
                start = x + 1
        return None

    def bold(self, text, nth=0):
        """True when the first visible character of `text` on screen is bold."""
        at = self.find(text, nth)
        if at is None:
            raise AssertionError(f"{text!r} is not on screen")
        offset = len(text) - len(text.lstrip())
        return bool(self.screen.buffer[at[1]][at[0] + offset].bold)

    def selected(self, text):
        """True when the focused explorer has its cursor on the row of `text`:
        the explorer draws '┃' left of that row only while it has the focus
        (bold alone isn't enough: the active file and folder stay bold).
        Every place `text` shows is tried: the menu bar and tabs repeat names."""
        if not self.shows(text):
            raise AssertionError(f"{text!r} is not on screen")
        return any("┃" in line[1:x]
                   for line in self.lines
                   for x in [i for i in range(len(line)) if line.startswith(text, i)])

    def cursor(self):
        return self.screen.cursor.x, self.screen.cursor.y

    def send(self, data, quiet=0.12):
        os.write(self.fd, data.encode() if isinstance(data, str) else data)
        self.pump(quiet)

    def key(self, *names):
        for name in names:
            self.send(KEYS[name])

    def type(self, text):
        for char in text:
            self.send(char, 0.03)
        self.pump(0.1)

    def command(self, text):
        """Types `>text` and Enter (Normal mode)."""
        self.send(">")
        self.type(text)
        self.key("enter")

    def click_at(self, x, y):
        self.send(f"\x1b[<0;{x + 1};{y + 1}M")
        self.send(f"\x1b[<0;{x + 1};{y + 1}m")

    def wheel(self, x, y, down=True, notches=1):
        for _ in range(notches):
            self.send(f"\x1b[<{65 if down else 64};{x + 1};{y + 1}M")

    def click(self, text, nth=0, dx=0):
        at = self.find(text, nth)
        if at is None:
            raise AssertionError(f"{text!r} is not on screen")
        self.click_at(at[0] + dx, at[1])

    def alive(self):
        if self.exit_code is not None:
            return False
        try:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
        except ChildProcessError:
            self.exit_code = -1
            return False
        if pid == 0:
            return True
        self.exit_code = os.waitstatus_to_exitcode(status)
        return False

    def wait_exit(self, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end and self.alive():
            self.pump(0.05, 0.1)
        return self.exit_code

    def signal(self, number):
        os.kill(self.pid, number)

    def close(self):
        if self.alive():
            try:
                os.kill(self.pid, signal.SIGKILL)
                os.waitpid(self.pid, 0)
            except (ProcessLookupError, ChildProcessError):
                pass
        try:
            os.close(self.fd)
        except OSError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class Checks:
    """Counts what passed and failed; a failed check prints the screen."""

    def __init__(self):
        self.passed = 0
        self.failed = []

    def check(self, ok, what, editor=None):
        if ok:
            self.passed += 1
            return
        self.failed.append(what)
        print(f"  FAIL  {what}")
        if editor is not None:
            for line in editor.lines:
                print("  |" + line.rstrip())
