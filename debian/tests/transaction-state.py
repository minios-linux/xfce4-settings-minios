#!/usr/bin/python3
"""Read-only assertions for unified Apply on a disposable single-output VM."""
import json
import math
import re
import subprocess
import sys


def command(*args):
    return subprocess.check_output(args, text=True, timeout=10).strip()


def property_value(name):
    return command("xfconf-query", "-c", "displays", "-p", name)


randr = command("xrandr", "--verbose")
state = {
    "choice": int(property_value("/GlobalScale")),
    "effective": int(property_value("/GlobalScale/Effective")),
    "pending": property_value("/GlobalScale/Pending"),
    "physical": list(map(int, re.search(
        r"^\s+\S+\s+\(0x[0-9a-fA-F]+\)[^\n]*\*current[^\n]*\n"
        r"\s+h: width\s+(\d+)[^\n]*\n\s+v: height\s+(\d+)",
        randr, re.M).groups())),
    "framebuffer": list(map(int, re.search(r"current (\d+) x (\d+)", randr).groups())),
    "properties": command("xfconf-query", "-c", "displays", "-lv"),
}
assert state["pending"] == "false", state
assert not re.search(r"^/Temp(?:/|\s)", state["properties"], re.M), state
assert property_value("/GlobalScale/Error") == "", state
if sys.argv[1] == "capture":
    assert state["choice"] == 0 and state["effective"] == 100, state
    assert state["physical"] != [1280, 960], "Choose a different transaction test mode"
elif sys.argv[1] == "restored":
    with open("/mnt/hidpi/transaction-baseline.json") as source:
        baseline = json.load(source)
    for key in ("choice", "effective", "physical", "framebuffer"):
        assert state[key] == baseline[key], (key, baseline, state)
elif sys.argv[1] == "kept":
    assert state["choice"] == 125 and state["effective"] == 125, state
    assert state["physical"] == [1280, 960], state
    factor = int(2 * 100 / 125 * 65536) / 65536
    assert state["framebuffer"] == [math.ceil(size * factor) for size in state["physical"]], state
    assert property_value("/Default/Virtual-1/Resolution") == "1280x960", state
else:
    raise AssertionError(sys.argv[1])
print(json.dumps(state, indent=2))
