"""look.py EXE [steps...]: runs the editor in a throwaway folder and prints
the screen after the steps. A step is a key name (driver.KEYS), "t:text"
(type), "c:text" (click), "cmd:text" (a command), "w:seconds" or "p" (print
now). "size:COLSxROWS" first sets the terminal's size."""

import sys

from driver import KEYS, Editor, Lab

exe, steps = sys.argv[1], sys.argv[2:]
cols, rows = 100, 30
if steps and steps[0].startswith("size:"):
    cols, rows = (int(n) for n in steps[0][5:].split("x"))
    steps = steps[1:]

with Lab() as lab:
    lab.write("alpha.txt", "alpha one\nalpha two\nalpha three\n")
    lab.write("beta.cpp", '#include <iostream>\n\nint main() {\n    std::cout << "hi";\n    return 0;\n}\n')
    lab.write("src/main.cpp", "int main() { return 0; }\n")
    lab.write("docs/readme.md", "# Title\n\nSome text.\n")
    with Editor(exe, lab=lab, cols=cols, rows=rows) as ed:
        def show():
            print(f"===== cursor {ed.cursor()}")
            for line in ed.lines:
                print("|" + line.rstrip())
        for step in steps:
            if step == "p":
                show()
            elif step.startswith("t:"):
                ed.type(step[2:])
            elif step.startswith("c:"):
                ed.click(step[2:])
            elif step.startswith("cmd:"):
                ed.command(step[4:])
            elif step.startswith("w:"):
                ed.pump(float(step[2:]), float(step[2:]) + 1)
            else:
                ed.key(step)
        show()
        print("alive:", ed.alive())
