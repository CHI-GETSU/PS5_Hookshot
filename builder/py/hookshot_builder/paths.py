"""Where things are: the builder's own folder in %LOCALAPPDATA%, and this app's folder."""
import os
import sys

APP_DIR = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(APP_DIR, "assets")


def base():
    """%LOCALAPPDATA%\\HookshotBuilder (HOOKSHOT_SETUP_BASE=<folder>: another, for testing installs and updates)"""
    b = os.environ.get("HOOKSHOT_SETUP_BASE")
    if b:
        return b
    return os.path.join(os.environ.get("LOCALAPPDATA") or os.path.expanduser("~"), "HookshotBuilder")


def versions():
    return os.path.join(base(), "versions")


def read_small(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read().strip()
    except OSError:
        return ""


def write_small(path, text):
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    except OSError:
        pass


def launcher_command():
    """How this app is started again (the game folder's launcher, the game's update offer, a shortcut): the Python that runs it
    (pythonw.exe beside python.exe when there is one: no console window) and the app's start script"""
    exe = sys.executable
    w = os.path.join(os.path.dirname(exe), "pythonw.exe")
    if os.path.isfile(w):
        exe = w
    return exe, ["-I", os.path.join(os.path.dirname(APP_DIR), "hookshot_builder_start.py")]
