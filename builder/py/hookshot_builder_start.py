"""Starts Hookshot Builder: python.exe -I hookshot_builder_start.py [--update <game folder> --tag <release> [--fetched]] [--launch
<game folder>] [--repair]. Qt for Python is the "qt" folder beside this file's (the package), or HOOKSHOT_QT (development)."""
import argparse
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def settle():
    """The package (python, qt, app) copied once into %LOCALAPPDATA%\\HookshotBuilder\\app and started from there: the shortcuts, the
    game folder's launcher and the game's update offer point at that copy, which stays where it is whatever happens to the folder
    the download was unpacked into. True: the copy was started, this one ends"""
    import shutil
    import subprocess
    root = os.path.dirname(HERE)
    version = os.path.join(HERE, "VERSION")
    if not os.path.isfile(version) or not os.path.isdir(os.path.join(root, "python")) or os.environ.get("HOOKSHOT_NO_SETTLE"):
        return False   # (a source checkout: run where it is)
    base = os.environ.get("HOOKSHOT_SETUP_BASE") or os.path.join(os.environ.get("LOCALAPPDATA") or os.path.expanduser("~"),
                                                                 "HookshotBuilder")
    home = os.path.join(base, "app")
    if os.path.normcase(os.path.abspath(root)) == os.path.normcase(os.path.abspath(home)):
        return False
    want = open(version, encoding="utf-8").read().strip()
    try:
        have = open(os.path.join(home, "app", "VERSION"), encoding="utf-8").read().strip()
    except OSError:
        have = ""
    if have != want:
        part = home + ".part"
        shutil.rmtree(part, ignore_errors=True)
        try:
            shutil.copytree(root, part, ignore=shutil.ignore_patterns("__pycache__"))
            shutil.rmtree(home, ignore_errors=True)
            os.replace(part, home)
        except OSError:
            shutil.rmtree(part, ignore_errors=True)
            return False   # (could not copy: run from here)
    exe = os.path.join(home, "python", "pythonw.exe")
    if not os.path.isfile(exe):
        exe = os.path.join(home, "python", "python.exe")
    subprocess.Popen([exe, "-I", os.path.join(home, "app", "hookshot_builder_start.py"), *sys.argv[1:]], cwd=home)
    return True


def main():
    p = argparse.ArgumentParser(add_help=False)
    p.add_argument("--update")
    p.add_argument("--tag")
    p.add_argument("--fetched", action="store_true")
    p.add_argument("--launch")
    p.add_argument("--repair", action="store_true")
    args, _ = p.parse_known_args()
    if settle():
        return
    # (the app's code: the newest of this folder's and the installed release's, so a release's fixes reach the window without a new
    # download of the package; the package's own when the release has none)
    sys.path.insert(0, HERE)
    try:
        from hookshot_builder import setup_parts
        cur = setup_parts.current()
        newer = os.path.join(cur, "HookshotBuilder", "tools", "builder_py") if cur else None
        if newer and os.path.isfile(os.path.join(newer, "hookshot_builder", "window.py")) and not os.environ.get("HOOKSHOT_OWN_APP"):
            for m in [m for m in sys.modules if m == "hookshot_builder" or m.startswith("hookshot_builder.")]:
                del sys.modules[m]
            sys.path.insert(0, newer)
    except Exception:
        pass
    qt = os.environ.get("HOOKSHOT_QT") or os.path.join(os.path.dirname(HERE), "qt")
    if os.path.isdir(qt):
        sys.path.insert(0, qt)
        os.add_dll_directory(qt)
    # (the taskbar groups and shows the window by this id and its own icon, not as python.exe)
    from hookshot_builder import APP_ID
    try:
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(APP_ID)
    except (AttributeError, OSError):
        pass
    try:   # (a console the start left, when started by python.exe rather than pythonw.exe: not shown)
        if ctypes.windll.kernel32.GetConsoleWindow() and not os.environ.get("HOOKSHOT_CONSOLE"):
            ctypes.windll.kernel32.FreeConsole()
    except OSError:
        pass
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QGuiApplication, QIcon
    from PySide6.QtWidgets import QApplication
    QGuiApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    app = QApplication(sys.argv[:1])
    app.setApplicationName("Hookshot")
    app.setApplicationDisplayName("Hookshot")
    from hookshot_builder import paths, theme
    from hookshot_builder.window import Window
    app.setWindowIcon(QIcon(os.path.join(paths.ASSETS, "hookshot.ico")))
    theme.load_fonts()
    w = Window(args)
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
