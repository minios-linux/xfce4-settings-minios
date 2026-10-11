#!/usr/bin/python3
"""Fresh-config gate with the package's actual system Xfconf defaults.

Run with private XDG_CONFIG_HOME/XDG_RUNTIME_DIR, XDG_CONFIG_DIRS pointing to
debian/tmp/etc/xdg, and a private D-Bus/Xvfb session.
"""
import pathlib
import os
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


def query(name):
    return subprocess.check_output(
        ["xfconf-query", "-c", "displays", "-p", name], text=True, timeout=10).strip()


def wait(name, value):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            if query(name) == value:
                return
        except subprocess.CalledProcessError:
            pass
        time.sleep(0.1)
    raise AssertionError((name, value, query(name)))


defaults = ET.parse(pathlib.Path(sys.argv[2]) / "xfce4/xfconf/xfce-perchannel-xml/displays.xml").getroot()
assert defaults.attrib == {"name": "displays", "version": "1.0"}
choice = defaults.find("property[@name='GlobalScale']")
assert choice is not None and choice.attrib["type"] == "int" and choice.attrib["value"] == "0"
if "--legacy-cache" in sys.argv:
    # Older unmanaged sessions wrote derived children under an empty parent.
    # Such a cache is not an explicit scale preference and must inherit Auto.
    cache = pathlib.Path(os.environ["XDG_CONFIG_HOME"]) / "xfce4/xfconf/xfce-perchannel-xml/displays.xml"
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_text('<channel name="displays" version="1.0">'
                     '<property name="GlobalScale" type="empty">'
                     '<property name="ToolkitScale" type="int" value="0"/>'
                     '<property name="Effective" type="int" value="100"/>'
                     '</property></channel>')
assert query("/GlobalScale") == "0", "Fresh Xfconf channel did not inherit Automatic"
daemon = None


def restart():
    global daemon
    if daemon is not None:
        daemon.terminate()
        daemon.wait(timeout=10)
    daemon = subprocess.Popen([sys.argv[1], "--replace", "--no-daemon"])
    time.sleep(1)
    assert daemon.poll() is None, daemon.returncode


try:
    restart()
    # Xvfb is virtual: Automatic resolves to 100%, but is managed rather than
    # the previous unmanaged fallback (ToolkitScale=0).
    wait("/GlobalScale/ToolkitScale", "1")
    wait("/GlobalScale/Effective", "100")
    assert query("/GlobalScale") == "0"
    print("PASS fresh startup inherits Automatic and initializes managed scaling")
    for value, toolkit in ((100, 1), (200, 2)):
        subprocess.run(["xfconf-query", "-c", "displays", "-p", "/GlobalScale",
                        "-s", str(value)], check=True, timeout=10)
        restart()
        wait("/GlobalScale/ToolkitScale", str(toolkit))
        wait("/GlobalScale/Effective", str(value))
        assert query("/GlobalScale") == str(value)
        print("PASS saved explicit scale takes precedence", value)
    subprocess.run(["xfconf-query", "-c", "displays", "-p", "/GlobalScale", "-r"],
                   check=True, timeout=10)
    assert query("/GlobalScale") == "0", "Reset must reveal the Automatic system default"
    restart()
    wait("/GlobalScale/ToolkitScale", "1")
    wait("/GlobalScale/Effective", "100")
    print("PASS reset restores Automatic system default")
finally:
    if daemon is not None:
        daemon.terminate()
        daemon.wait(timeout=10)
