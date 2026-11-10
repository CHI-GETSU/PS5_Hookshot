"""Renders the window's pages to PNGs without showing it (QT_QPA_PLATFORM=offscreen): python -I shots.py <out folder>. For comparing
the layout with the WPF builder's screenshots."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
qt = os.environ.get("HOOKSHOT_QT")
if qt:
    sys.path.insert(0, qt)
    os.add_dll_directory(os.path.join(qt, "PySide6"))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
out = sys.argv[1]
os.makedirs(out, exist_ok=True)

from PySide6.QtCore import QTimer  # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402

app = QApplication(sys.argv[:1])
from hookshot_builder import theme, window  # noqa: E402

theme.load_fonts()


class Args:
    update = tag = launch = None
    fetched = repair = False


pages = ["files", "build", "ready", "problem", "update", "launch", "splash"]
state = {"i": 0, "w": None}


def next_page():
    if state["w"] is not None:
        state["w"].grab().save(os.path.join(out, pages[state["i"] - 1] + ".png"))
        state["w"].close()
    if state["i"] >= len(pages):
        app.quit()
        return
    name = pages[state["i"]]
    state["i"] += 1
    if name not in ("files", "splash"):
        os.environ["HOOKSHOT_UI_PREVIEW"] = name
    else:
        os.environ.pop("HOOKSHOT_UI_PREVIEW", None)
    w = window.Window(Args())
    if name == "splash":
        w.start = lambda: None
        w.splash.text = "Looking for your game files…"
    w.show()
    state["w"] = w
    QTimer.singleShot(2600 if name != "splash" else 600, next_page)


QTimer.singleShot(0, next_page)
app.exec()
print("wrote", out)
