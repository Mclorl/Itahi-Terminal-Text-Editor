"""run_all.py EXE: scripted sessions of the real editor. Exit code 1 when a
check fails. Needs pyte (pip install pyte)."""

import os
import signal
import sys
import time

from driver import Checks, Editor, Lab


def project(lab):
    lab.write("alpha.txt", "alpha one\nalpha two\nalpha three\n")
    lab.write("beta.cpp", '#include <iostream>\n\nint main() {\n    std::cout << "hi";\n    return 0;\n}\n')
    lab.write("src/main.cpp", "int main() { return 0; }\n")
    lab.write("src/util/helper.hpp", "#pragma once\n")


def explorer_keys(exe, c):
    """The arrows never leave the explorer (Up on the first row used to jump
    to the menu bar)."""
    with Lab() as lab:
        project(lab)
        with Editor(exe, lab=lab) as ed:
            c.check(ed.bold("> src"), "the editor starts on the explorer's first row", ed)
            ed.key("up")
            c.check(ed.bold("> src") and not ed.bold("File"), "Up on the first row stays on it", ed)
            ed.key("down", "down", "down", "down", "down")
            c.check(ed.bold("beta.cpp"), "Down past the last row stays on it", ed)
            ed.key("right")
            c.check(ed.bold("beta.cpp"), "Right on a file stays in the explorer", ed)
            ed.key("up", "up")
            ed.key("right")
            c.check(ed.shows("v src") and ed.shows("main.cpp"), "Right opens a folder", ed)
            ed.key("right")
            c.check(ed.bold("> util"), "Right again goes into it", ed)
            ed.key("left")
            c.check(ed.bold("v src"), "Left goes to the folder above", ed)
            ed.key("left")
            c.check(ed.shows("> src") and not ed.shows("main.cpp"), "Left closes the folder", ed)
            ed.key("ctrl-up")
            c.check(ed.bold("File"), "Ctrl+Up goes to the menu bar", ed)
            ed.key("ctrl-down")
            c.check(ed.bold("> src"), "Ctrl+Down comes back to the explorer", ed)
            ed.key("down", "enter")
            c.check(ed.shows("alpha one"), "Enter opens the file", ed)
            ed.send("e")
            ed.type("X")
            ed.key("esc")
            c.check(ed.shows("Xalpha one"), "and the keys go to its text", ed)


def windows(exe, c):
    with Lab() as lab:
        project(lab)
        with Editor(exe, lab=lab) as ed:
            ed.click("alpha.txt")
            ed.command("vsplit")
            c.check(ed.shows(" 1  alpha.txt") and ed.shows(" 2  alpha.txt"), ">vsplit: two windows side by side", ed)
            ed.type("w")
            ed.send("s")
            c.check(ed.shows(" 3  alpha.txt"), "w s: a third window, below", ed)
            ed.key("ctrl-backslash")
            c.check(ed.shows(" 4  alpha.txt") or ed.shows(" 2  alpha.txt") and ed.shows("Win 2/4"), "Ctrl+\\: a fourth", ed)
            ed.command("split")
            c.check(ed.shows("Four windows are open"), "a fifth is refused", ed)
            ed.key("esc")
            ed.command("only")
            c.check(not ed.shows(" 2  alpha.txt"), ">only: one window again", ed)

            # two windows, a file each: the keys and the mouse go to the right one.
            ed.command("vsplit beta.cpp")
            c.check(ed.shows(" 1  alpha.txt") and ed.shows(" 2  beta.cpp"), ">vsplit file opens it in the new window", ed)
            ed.key("ctrl-left")
            ed.send("e")
            ed.type("LEFT")
            ed.key("esc")
            c.check(ed.shows("LEFTalpha one"), "Ctrl+Left: the left window takes the keys", ed)
            ed.key("ctrl-left")
            c.check(ed.bold("alpha.txt", 0) or ed.bold("> src"), "Ctrl+Left again: the explorer", ed)
            ed.key("ctrl-right", "ctrl-right")
            ed.send("e")
            ed.type("RIGHT")
            ed.key("esc")
            c.check(ed.shows("RIGHT#include"), "Ctrl+Right twice: the right window", ed)
            ed.key("alt-shift-left")
            line = next(l for l in ed.lines if " 1  " in l)
            c.check(" 1  beta.cpp" in line and " 2  alpha.txt" in line, "Alt+Shift+Left: the windows change places", ed)
            ed.type("w")
            ed.send("J")
            c.check(ed.find(" 2  beta.cpp") and ed.find(" 1  alpha.txt")
                    and ed.find(" 2  beta.cpp")[1] > ed.find(" 1  alpha.txt")[1], "w J: it goes below the other", ed)
            ed.click("alpha one")
            ed.key("esc")  # a key: the last message gives way to the status bar
            c.check(ed.shows("Win 1/2"), "a click gives a window the focus", ed)
            ed.key("f6")
            c.check(ed.shows("Win 2/2"), "F6: the next window", ed)
            title = ed.find(" 2  beta.cpp")
            ed.click_at(ed.lines[title[1]].rindex(" x ") + 1, title[1])
            ed.pump(0.3)
            c.check(not ed.shows(" 2  "), "a click on a window's x closes it", ed)
            ed.command("ls")
            c.check(ed.shows("alpha.txt*") and ed.shows("beta.cpp*"), "its file is still open", ed)


def menus(exe, c):
    with Lab() as lab:
        project(lab)
        with Editor(exe, lab=lab) as ed:
            ed.click("View")
            row = next((l for l in ed.lines if "< Split Right >" in l), "")
            c.check(row.index("<") >= 30 if row else False, "menu rows have a margin on the left", ed)
            c.check(ed.shows("Windows") and ed.shows("w v   >vsplit"), "the page has titles and shortcuts", ed)
            ed.click("< Split Right >")
            c.check(ed.shows("Window 2 of 2") or ed.shows(" 2  "), "a button runs its command", ed)
            ed.click("Settings")
            ed.click("< Keep cursor centered v >")
            c.check(ed.shows("(*) Keep cursor centered") and ed.shows("( ) Scroll freely"), "the dropdown opens as a list", ed)
            ed.key("down", "enter")
            c.check(ed.shows("< Scroll freely") and not ed.shows("( ) "), "arrows and Enter choose from it", ed)
            ed.key("left")
            c.check(not ed.bold("> src"), "Left on a menu row doesn't jump to the explorer", ed)
            ed.key("f1")
            c.check(ed.shows("Keys and commands") and ed.shows("Moving around"), "F1 shows the keys and commands", ed)
            ed.click("<-")
            ed.pump(0.3)
            c.check(ed.shows("NORMAL") or ed.shows("Welcome"), "<- goes back to the editor", ed)
            config = os.path.join(lab.home, ".config", "itahi", "config")
            c.check(os.path.exists(config) and "scroll_style = free" in open(config).read(), "the choice is saved")


def menu_scrolling(exe, c):
    """A menu page taller than the window scrolls: with the wheel wherever
    the focus is, and with the keys."""
    with Lab() as lab:
        project(lab)
        with Editor(exe, lab=lab, cols=100, rows=20) as ed:
            ed.click("View")  # the focus is on the menu bar, not in the page
            c.check(ed.shows("Split Right") and not ed.shows("Show Line Numbers"), "the View page is taller than the window", ed)
            ed.wheel(60, 10, down=True, notches=4)
            c.check(ed.shows("Show Line Numbers") and not ed.shows("Split Right"), "the wheel scrolls it without the focus", ed)
            ed.wheel(60, 10, down=True, notches=30)
            c.check(ed.shows("Diff Removed"), "down to its last row", ed)
            ed.wheel(60, 10, down=False, notches=40)
            c.check(ed.shows("Windows") and ed.shows("Split Right"), "and back up to its title", ed)
            ed.click("< Split Right >")  # runs, and leaves the menu: back in
            ed.click("View")
            ed.key("ctrl-down")
            for _ in range(22):
                ed.key("down")
            c.check(ed.shows("Theme") and not ed.shows("Split Right"), "the keys scroll to the row they move to", ed)
            for _ in range(40):
                ed.key("up")
            c.check(ed.shows("Windows") and ed.shows("Split Right"), "and back to the top, title included", ed)

            ed.click("Settings")
            ed.wheel(60, 10, down=True, notches=10)
            c.check(ed.shows("Read Files Again When They Change On Disk"), "the Settings page scrolls too", ed)
            ed.click("Keep Unsaved Changes For Recovery")
            c.check(ed.shows("[ ] Keep Unsaved Changes"), "a row scrolled into view takes clicks", ed)
            ed.click("Keep Unsaved Changes For Recovery")
            ed.click("Help")
            ed.wheel(60, 10, down=True, notches=20)
            c.check(ed.shows(">vsplit") and not ed.shows("Moving around"), "the Help list scrolls with the wheel", ed)


def files(exe, c):
    with Lab() as lab:
        project(lab)
        with Editor(exe, lab=lab) as ed:
            # rename and delete from the explorer.
            ed.key("down")  # alpha.txt
            ed.key("f2")
            c.check(ed.shows(">rename alpha.txt") and ed.shows("renames ./alpha.txt"), "F2 asks on the command line", ed)
            for _ in range(len("alpha.txt")):
                ed.key("backspace")
            ed.type("gamma.txt")
            ed.key("enter")
            c.check(lab.exists("gamma.txt") and not lab.exists("alpha.txt") and ed.shows("gamma.txt"), "and renames the file", ed)
            ed.key("down")
            ed.key("delete")
            c.check(ed.shows("to the trash"), "Delete asks first", ed)
            ed.key("enter")
            c.check(not lab.exists("gamma.txt") or not lab.exists("beta.cpp"), "Enter moves it to the trash", ed)
            trash = os.path.join(lab.home, ".local", "share", "Trash", "files")
            c.check(os.path.isdir(trash) and len(os.listdir(trash)) == 1, "where it can be restored from")

            # a file another program makes, and one it changes, show up by themselves.
            lab.write("from_outside.txt", "made elsewhere\n")
            c.check(ed.wait_for("from_outside.txt", 4), "a new file appears in the explorer by itself", ed)
            ed.click("from_outside.txt")
            c.check(ed.shows("made elsewhere"), "it opens", ed)
            lab.write("from_outside.txt", "changed elsewhere, twice as long\n")
            c.check(ed.wait_for("changed elsewhere", 4), "and is read again when it changes on disk", ed)
            ed.send("e")
            ed.type("mine ")
            ed.key("esc")
            time.sleep(0.05)
            lab.write("from_outside.txt", "a third version from outside\n")
            c.check(ed.wait_for("changed on disk", 4) and ed.shows("mine changed elsewhere"), "but never over unsaved changes", ed)
            ed.command("w")
            c.check(ed.shows("changed on disk") and "third version" in lab.read("from_outside.txt"), ">w refuses to overwrite it", ed)
            ed.key("esc")

            # the command history.
            ed.command("ls")
            ed.send(">")
            ed.key("up")
            c.check(ed.shows(">ls"), "Up in the command line brings the last command back", ed)
            ed.key("esc")


def recovery(exe, c):
    with Lab() as lab:
        project(lab)
        with Editor(exe, lab=lab) as ed:
            ed.click("alpha.txt")
            ed.send("e")
            ed.type("not saved ")
            ed.key("esc")
            ed.pump(3.0, 3.5)  # a moment of rest: the copy is written
            ed.signal(signal.SIGKILL)
            ed.wait_exit()
        c.check(lab.read("alpha.txt").startswith("alpha one"), "the file itself is untouched")
        with Editor(exe, lab=lab) as ed:
            c.check(ed.shows(">recover"), "the next start says changes were kept", ed)
            ed.command("recover")
            c.check(ed.shows("not saved alpha one") and ed.shows("alpha.txt*"), ">recover brings them back, unsaved", ed)
            ed.key("ctrl-s")
            c.check(lab.read("alpha.txt").startswith("not saved alpha one"), "and they can be saved")
            ed.send("e")
            ed.type("again ")
            ed.key("esc")
            ed.signal(signal.SIGTERM)  # the terminal is closed: kept at once
            c.check(ed.wait_exit() is not None, "the editor ends on SIGTERM")
        with Editor(exe, lab=lab) as ed:
            c.check(ed.shows(">recover"), "changes are kept when the editor is closed from outside", ed)
            ed.command("recover!")
            c.check(ed.shows("Forgot"), ">recover! forgets them", ed)
            ed.key("ctrl-c")
            c.check(ed.wait_exit() == 0, "Ctrl+C quits when nothing is unsaved")


def main():
    exe = os.path.abspath(sys.argv[1])
    failed = 0
    total = 0
    for scenario in (explorer_keys, windows, menus, menu_scrolling, files, recovery):
        checks = Checks()
        try:
            scenario(exe, checks)
        except Exception as error:  # a crash of the scenario is a failure too
            checks.check(False, f"{type(error).__name__}: {error}")
        total += checks.passed + len(checks.failed)
        failed += len(checks.failed)
        print(f"{scenario.__name__}: {checks.passed}/{checks.passed + len(checks.failed)}")
    print(f"{total - failed} of {total} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
