#!/usr/bin/python3
"""Production XSettings cursor gate; run in isolated D-Bus/Xvfb/config dirs."""
import os
import subprocess
import sys
import time
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk


def set_value(channel, name, kind, value):
    subprocess.run(["xfconf-query", "-c", channel, "-p", name, "-n", "-t", kind,
                    "-s", str(value)], check=True, timeout=10)


def pump():
    while Gtk.events_pending():
        Gtk.main_iteration_do(False)


def wait_size(size):
    end = time.monotonic() + 5
    while time.monotonic() < end:
        pump()
        if settings.get_property("gtk-cursor-theme-size") == size:
            resources = subprocess.check_output(["xrdb", "-query"], text=True)
            if any(line.split() == ["Xcursor.size:", str(size)] for line in resources.splitlines()):
                return
        time.sleep(0.05)
    raise AssertionError((size, settings.get_property("gtk-cursor-theme-size")))


set_value("xsettings", "/Gtk/CursorThemeName", "string", "Adwaita")
set_value("xsettings", "/Gtk/CursorThemeSize", "int", 24)
daemon = subprocess.Popen([sys.argv[1], "--replace", "--no-daemon"])
try:
    time.sleep(1)
    Gtk.init([])
    settings = Gtk.Settings.get_default()
    wait_size(24)
    # Long-lived GTK client: this checks generated last-change serials too.
    for scale, expected in ((2, 48), (1, 24), (2, 48), (0, 24)):
        set_value("displays", "/GlobalScale/ToolkitScale", "int", scale)
        wait_size(expected)
        print("PASS cursor publication", scale, expected)
    set_value("displays", "/GlobalScale/ToolkitScale", "int", 2)
    set_value("xsettings", "/Gtk/CursorThemeSize", "int", 32)
    wait_size(64)
    set_value("xsettings", "/Gtk/CursorThemeSize", "int", 0)
    end = time.monotonic() + 5
    while time.monotonic() < end:
        pump()
        automatic_size = settings.get_property("gtk-cursor-theme-size")
        if automatic_size != 64 and automatic_size > 0:
            break
        time.sleep(0.05)
    assert automatic_size > 0 and automatic_size % 2 == 0, automatic_size
    fallback = automatic_size // 2
    wait_size(automatic_size)
    set_value("displays", "/GlobalScale/ToolkitScale", "int", 0)
    wait_size(fallback)
    print("PASS automatic-size rollback")
    set_value("displays", "/GlobalScale/ToolkitScale", "int", 2)
    wait_size(automatic_size)
    daemon.terminate()
    daemon.wait(timeout=10)
    daemon = subprocess.Popen([sys.argv[1], "--replace", "--no-daemon"])
    # The display backend starts unmanaged in this test; it must not preserve
    # the previous published 48 as another logical fallback.
    wait_size(fallback)
    value = subprocess.check_output(["xfconf-query", "-c", "xsettings", "-p",
                                     "/Gtk/CursorThemeSize"], text=True).strip()
    assert value == "0", value
    print("PASS replacement does not compound cursor size; xfconf remains zero")
finally:
    daemon.terminate()
    daemon.wait(timeout=10)
