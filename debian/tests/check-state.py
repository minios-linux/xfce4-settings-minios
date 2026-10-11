#!/usr/bin/python3
"""Read-only acceptance check after a new X11 login."""
import json
import math
import os
import re
import subprocess
import sys

def command(*args):
    return subprocess.check_output(args, text=True, timeout=10).strip()

expected = int(sys.argv[1])
choice = int(command("xfconf-query", "-c", "displays", "-p", "/GlobalScale"))
effective = int(command("xfconf-query", "-c", "displays", "-p", "/GlobalScale/Effective"))
assert choice == expected and effective == expected, (choice, effective, expected)
assert os.environ.get("QT_ENABLE_HIGHDPI_SCALING") == "1"
randr = command("xrandr", "--verbose")
physical = tuple(map(int, re.search(
    r"^\s+\S+\s+\(0x[0-9a-fA-F]+\)[^\n]*\*current[^\n]*\n"
    r"\s+h: width\s+(\d+)[^\n]*\n\s+v: height\s+(\d+)",
    randr, re.M).groups()))
framebuffer = tuple(map(int, re.search(r"current (\d+) x (\d+)", randr).groups()))
toolkit = 1 if expected == 100 else 2
factor = int(toolkit * 100 / expected * 65536) / 65536
assert framebuffer == tuple(math.ceil(size * factor) for size in physical), (physical, framebuffer)
gtk = json.loads(command(sys.executable, "/mnt/hidpi/live-scale.py", "--probe"))
assert gtk["gtk_scale"] == toolkit and gtk["gtk_dpi"] == 96 * 1024, gtk
print(json.dumps({"choice": choice, "effective": effective, "physical": physical,
                  "framebuffer": framebuffer, "gtk": gtk}, indent=2))
