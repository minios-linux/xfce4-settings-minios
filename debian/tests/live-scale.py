#!/usr/bin/python3
"""Acceptance probe for an isolated, disposable MiniOS X11 session.

No runtime scaling implementation lives here: requests exercise xfsettingsd.
The caller must install the candidate packages and replace xfsettingsd/xfwm4.
"""
import json
import math
import os
import re
import subprocess
import sys
import time


def command(*args):
    return subprocess.check_output(args, text=True, timeout=8).strip()


def get(prop, default=None):
    result = subprocess.run(["xfconf-query", "-c", "displays", "-p", prop],
                            capture_output=True, text=True, timeout=8)
    return result.stdout.strip() if result.returncode == 0 else default


def set_value(prop, value, kind):
    subprocess.run(["xfconf-query", "-c", "displays", "-p", prop, "-r"],
                   capture_output=True)
    subprocess.check_call(["xfconf-query", "-c", "displays", "-p", prop,
                           "-n", "-t", kind, "-s", str(value)], timeout=8)


def wait_value(prop, value, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if get(prop) == str(value):
            return
        time.sleep(0.1)
    raise AssertionError((prop, value, get(prop), get("/GlobalScale/Error")))


def probe(anchor=False):
    import gi
    gi.require_version("Gtk", "3.0")
    gi.require_version("GdkX11", "3.0")
    from gi.repository import Gtk, GdkX11
    window = Gtk.Window(title="HiDPI GTK3 probe")
    button = Gtk.Button(label="Interface scale")
    window.add(button)
    if anchor:
        window.set_accept_focus(False)
        window.set_default_size(240, 120)
        window.move(200, 160)
    window.show_all()
    deadline = time.monotonic() + 0.5
    while time.monotonic() < deadline:
        while Gtk.events_pending():
            Gtk.main_iteration_do(False)
        time.sleep(0.01)
    xid = GdkX11.X11Window.get_xid(window.get_window())
    if anchor:
        print(json.dumps({"xid": xid}), flush=True)
        Gtk.main()
        return
    extents = command("xprop", "-id", str(xid), "_NET_FRAME_EXTENTS")
    result = {
        "gtk_scale": window.get_scale_factor(),
        "gtk_dpi": Gtk.Settings.get_default().get_property("gtk-xft-dpi"),
        "button_logical_width": button.get_allocated_width(),
        "button_logical_height": button.get_allocated_height(),
        "frame_extents": extents,
        "xid": xid,
        "x_geometry": command("xwininfo", "-id", str(xid)),
    }
    window.destroy()
    print(json.dumps(result))


def main():
    baseline = command("xfconf-query", "-c", "xsettings", "-lv")
    results = []
    toolkit_baselines = {}
    anchor = subprocess.Popen([sys.executable, "-u", __file__, "--anchor"],
                              stdout=subprocess.PIPE, text=True)
    anchor_xid = json.loads(anchor.stdout.readline())["xid"]
    anchor_baseline = None
    frame_baseline = None
    for percent in (100, 200, 100, 125, 150, 175, 200, 100):
        set_value("/GlobalScale/Request", percent, "int")
        wait_value("/GlobalScale/Pending", "true")
        wait_value("/GlobalScale/Effective", percent)
        toolkit = 1 if percent == 100 else 2
        time.sleep(0.5)
        measured = json.loads(command(sys.executable, __file__, "--probe"))
        measured["choice"] = percent
        measured["randr"] = command("xrandr", "--verbose")
        framebuffer = tuple(map(int, re.search(r"current (\d+) x (\d+)", measured["randr"]).groups()))
        physical = tuple(map(int, re.search(r"h: width\s+(\d+).*?v: height\s+(\d+)", measured["randr"], re.S).groups()))
        factor = int((1 if percent == 100 else 2) * 100 / percent * 65536) / 65536
        assert framebuffer == tuple(math.ceil(size * factor) for size in physical), (percent, physical, framebuffer)
        measured["xresources"] = command("xrdb", "-query")
        assert measured["gtk_scale"] == toolkit, measured
        assert measured["gtk_dpi"] == 96 * 1024, measured
        width = int(re.search(r"Width:\s+(\d+)", measured["x_geometry"]).group(1))
        assert width == measured["button_logical_width"] * toolkit, measured
        measured["toolkits"] = {}
        for name in ("qt5-scale-probe", "qt6-scale-probe", "gtk4-scale-probe"):
            data = json.loads(command("/mnt/hidpi/" + name))
            assert data["scale"] == toolkit, (percent, name, data)
            if name not in toolkit_baselines:
                toolkit_baselines[name] = data
            else:
                reference = toolkit_baselines[name]
                assert abs(data["logical_width"] / reference["logical_width"] - 1) < 0.05, (name, reference, data)
                if "font_points" in data:
                    assert abs(data["font_points"] - reference["font_points"]) < 0.01, (name, reference, data)
            measured["toolkits"][name] = data
        anchor_geometry = command("xwininfo", "-id", str(anchor_xid))
        anchor_extents = command("xprop", "-id", str(anchor_xid), "_NET_FRAME_EXTENTS")
        left, right, top, bottom = map(int, anchor_extents.split("=")[1].strip().split(","))
        if frame_baseline is None:
            frame_baseline = (left, right, top, bottom)
        assert (left, right, top, bottom) == tuple(size * toolkit for size in frame_baseline), (percent, frame_baseline, anchor_extents)
        x = int(re.search(r"Absolute upper-left X:\s+(-?\d+)", anchor_geometry).group(1))
        y = int(re.search(r"Absolute upper-left Y:\s+(-?\d+)", anchor_geometry).group(1))
        compensation = toolkit * 100 / percent
        physical_anchor = ((x - left) / compensation, (y - top) / compensation)
        print("HIDPI_FRAME", percent, physical_anchor, "client", x, y, "extents", left, right, top, bottom, flush=True)
        if anchor_baseline is None:
            anchor_baseline = physical_anchor
        assert max(abs(a - b) for a, b in zip(anchor_baseline, physical_anchor)) <= 3, (percent, anchor_baseline, physical_anchor)
        measured["physical_frame_anchor"] = physical_anchor
        if toolkit == 2:
            assert re.search(r"Xft.dpi:\s+192(?:\s|$)", measured["xresources"]), measured
        assert command("xfconf-query", "-c", "xsettings", "-lv") == baseline
        set_value("/GlobalScale/Confirm", "true", "bool")
        wait_value("/GlobalScale/Pending", "false")
        wait_value("/GlobalScale", percent)
        results.append(measured)
        print("HIDPI_SCALE_PASS", percent, flush=True)
    set_value("/GlobalScale/Request", 150, "int")
    wait_value("/GlobalScale/Pending", "true")
    wait_value("/GlobalScale/Pending", "false", seconds=20)
    wait_value("/GlobalScale/Effective", 100)
    wait_value("/GlobalScale", 100)
    print("HIDPI_TIMEOUT_PASS", flush=True)
    set_value("/GlobalScale/Request", 0, "int")
    wait_value("/GlobalScale/Pending", "true")
    # Testo's QXL output is virtual: Automatic must conservatively use 100%.
    wait_value("/GlobalScale/Effective", 100)
    set_value("/GlobalScale/Confirm", "true", "bool")
    wait_value("/GlobalScale", 0)
    print("HIDPI_AUTOMATIC_PASS", flush=True)
    anchor.terminate()
    anchor.wait(timeout=5)
    with open("/mnt/hidpi/evidence.json", "w") as output:
        json.dump(results, output, indent=2)


if __name__ == "__main__":
    if "--probe" in sys.argv:
        probe()
    elif "--anchor" in sys.argv:
        probe(anchor=True)
    else:
        try:
            main()
        finally:
            print(command("ps", "-eLo", "pid,tid,stat,wchan:25,comm"), flush=True)
            print(command("sudo", "dmesg")[-12000:], flush=True)
            for log in ("/tmp/hidpi-xfsettingsd.log", "/tmp/hidpi-xfwm4.log"):
                if os.path.exists(log):
                    print(log, flush=True)
                    with open(log) as output:
                        print(output.read()[-6000:], flush=True)
            for log in ("/var/log/Xorg.0.log", os.path.expanduser("~/.local/share/xorg/Xorg.0.log")):
                result = subprocess.run(["sudo", "cat", log], capture_output=True, text=True, timeout=8)
                print(log, result.stdout[-12000:], flush=True)
