"""The Python Hookshot Builder's download: one zip with python.org's own Python (python.exe and pythonw.exe, signed by the Python
Software Foundation), Qt for Python's DLLs (signed by The Qt Company), the builder's code (builder/py) and the .bat that starts it.
Nothing in it is an executable of Hookshot's own.

    python -I scripts/make_py_builder.py --python <python folder> --qt <PySide6 site folder> --tag <tag> <out zip>

--python: a python.org Windows embeddable Python of the version the build tools use (its pythonw.exe starts the window without a
console). --qt: a folder holding PySide6 and shiboken6 (the two wheels unpacked). Only the parts of Qt the window uses are taken:
QtCore, QtGui, QtWidgets, the Windows platform plugin and the icon and image plugins (~40 MB of the wheel's ~200)."""
import argparse
import os
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOP = "Hookshot Builder"
QT_KEEP_DLLS = ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Svg.dll", "pyside6.abi3.dll", "msvcp140.dll", "msvcp140_1.dll",
                "msvcp140_2.dll", "vcruntime140.dll", "vcruntime140_1.dll", "concrt140.dll")
QT_KEEP_PY = ("QtCore.pyd", "QtGui.pyd", "QtWidgets.pyd", "__init__.py", "_config.py", "_git_pyside_version.py")
QT_PLUGINS = ("platforms/qwindows.dll", "imageformats/qico.dll", "imageformats/qjpeg.dll", "imageformats/qsvg.dll",
              "styles/qmodernwindowsstyle.dll")
BAT = ('@echo off\r\n'
       'rem Hookshot Builder: python.org\'s pythonw.exe runs the builder (app\\hookshot_builder_start.py) with no console window.\r\n'
       'start "" "%~dp0python\\pythonw.exe" -I "%~dp0app\\hookshot_builder_start.py" %*\r\n')


def add_tree(z, src, dst, keep=lambda rel: True):
    n = 0
    for d, ds, fs in os.walk(src):
        ds[:] = sorted(x for x in ds if x != "__pycache__")
        for f in sorted(fs):
            full = os.path.join(d, f)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            if keep(rel):
                z.write(full, "%s/%s/%s" % (TOP, dst, rel))
                n += 1
    return n


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--python", required=True)
    p.add_argument("--qt", required=True)
    p.add_argument("--tag", required=True)
    p.add_argument("out")
    a = p.parse_args()
    for need in ("python.exe", "pythonw.exe"):
        if not os.path.isfile(os.path.join(a.python, need)):
            raise SystemExit("%s has no %s (use python.org's Windows embeddable package)" % (a.python, need))
    pyside, shiboken = os.path.join(a.qt, "PySide6"), os.path.join(a.qt, "shiboken6")
    if not os.path.isdir(pyside) or not os.path.isdir(shiboken):
        raise SystemExit("%s has no PySide6 and shiboken6" % a.qt)

    def qt_keep(rel):
        if "/" not in rel:
            return rel in QT_KEEP_DLLS or rel in QT_KEEP_PY
        return rel.startswith("plugins/") and rel[len("plugins/"):] in QT_PLUGINS

    with zipfile.ZipFile(a.out + ".new", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr("%s/Hookshot Builder.bat" % TOP, BAT)
        n = add_tree(z, a.python, "python")
        n += add_tree(z, pyside, "qt/PySide6", qt_keep)
        n += add_tree(z, shiboken, "qt/shiboken6")
        py = os.path.join(ROOT, "builder", "py")
        n += add_tree(z, py, "app", lambda rel: rel not in ("gen_strings.py", "shots.py"))
        z.writestr("%s/app/VERSION" % TOP, a.tag + "\n")
        for lic in ("LICENSE",):
            z.write(os.path.join(ROOT, lic), "%s/%s" % (TOP, lic))
    os.replace(a.out + ".new", a.out)
    print("%s: %d files, %d MB" % (a.out, n, os.path.getsize(a.out) >> 20))


if __name__ == "__main__":
    main()
