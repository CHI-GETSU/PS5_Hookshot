"""The WPF builder's App.xaml in Qt: the Slate palette, the Inter fonts, and the controls drawn as its templates draw them (the same
sizes, radii, colours, glows and hover states)."""
import math
import os

from PySide6.QtCore import QPointF, QRectF, QSize, Qt, Signal
from PySide6.QtGui import QBrush, QColor, QFont, QFontDatabase, QLinearGradient, QPainter, QPainterPath, QPen
from PySide6.QtWidgets import (QAbstractButton, QComboBox, QFrame, QGraphicsDropShadowEffect, QLabel, QLineEdit, QListView,
                               QSizePolicy, QStyledItemDelegate, QWidget)

from . import paths


def argb(s):
    """#AARRGGBB or #RRGGBB, as WPF writes them"""
    s = s.lstrip("#")
    if len(s) == 8:
        return QColor(int(s[2:4], 16), int(s[4:6], 16), int(s[6:8], 16), int(s[0:2], 16))
    return QColor(int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


C = {k: argb(v) for k, v in {
    "Page": "#14161A", "Raised": "#1C1F24", "RaisedHover": "#23272D", "Line": "#2C3037", "LineStrong": "#3D434C",
    "Ink": "#EDEFF2", "InkSoft": "#A2A9B3", "InkFaint": "#6E7681", "Brand": "#3D63E6", "BrandHover": "#4B70F0",
    "BrandInk": "#DFE6FF", "Go": "#5FB08A", "GoHover": "#70C09A", "GoInk": "#06150E", "Warn": "#E2B86B", "Bad": "#E2776B",
    "Glass": "#0DFFFFFF", "GlassHover": "#17FFFFFF", "GlassLine": "#17FFFFFF", "GlassLineStrong": "#2AFFFFFF",
    "GoSoft": "#265FB08A", "BadSoft": "#26E2776B", "WarnSoft": "#1FE2B86B", "BrandSoft": "#333D63E6", "White": "#FFFFFF",
}.items()}

UI = "Inter"
MONO = "Cascadia Mono"
W_MEDIUM, W_SEMIBOLD, W_BOLD = QFont.Weight.Medium, QFont.Weight.DemiBold, QFont.Weight.Bold


def load_fonts():
    global MONO
    for f in ("Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf"):
        QFontDatabase.addApplicationFont(os.path.join(paths.ASSETS, "Fonts", f))
    if MONO not in QFontDatabase.families():
        MONO = "Consolas"


def font(size=14, weight=QFont.Weight.Normal, family=None):
    f = QFont(family or UI)
    f.setPixelSize(int(size))
    f.setWeight(weight)
    f.setHintingPreference(QFont.HintingPreference.PreferNoHinting)   # (WPF's Ideal text formatting)
    f.setStyleStrategy(QFont.StyleStrategy.PreferAntialias)
    return f


def css(c):
    return "rgba(%d,%d,%d,%d)" % (c.red(), c.green(), c.blue(), c.alpha())


def label(text="", size=14, weight=QFont.Weight.Normal, color="Ink", wrap=False, mono=False):
    l = QLabel(text)
    l.setFont(font(size, weight, MONO if mono else None))
    set_color(l, color)
    l.setWordWrap(wrap)
    l.setTextInteractionFlags(Qt.TextInteractionFlag.NoTextInteraction)
    l.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
    return l


def set_color(l, color):
    c = C[color] if isinstance(color, str) else color
    l.setStyleSheet("color: %s; background: transparent;" % css(c))


# the text styles of App.xaml
def heading(text=""):
    return label(text, 24, W_BOLD)


def sub(text=""):
    l = label(text, 14, color="InkSoft", wrap=True)
    l.setContentsMargins(0, 6, 0, 0)
    return l


def note(text="", size=13):
    return label(text, size, color="InkSoft", wrap=True)


def faint(text="", size=12):
    return label(text, size, color="InkFaint")


def section(text):
    l = label(text, 12, W_SEMIBOLD, "InkSoft")
    l.setContentsMargins(0, 24, 0, 10)
    return l


def glow(widget, color, blur, depth, opacity):
    """WPF's DropShadowEffect (Direction 270: straight down)"""
    e = QGraphicsDropShadowEffect(widget)
    c = QColor(argb(color) if isinstance(color, str) else color)
    c.setAlphaF(opacity)
    e.setColor(c)
    e.setBlurRadius(blur * 1.2)
    e.setOffset(0, depth)
    widget.setGraphicsEffect(e)
    return e


def rounded(rect, r):
    p = QPainterPath()
    p.addRoundedRect(rect, r, r)
    return p


class Card(QFrame):
    """Card: Glass background, GlassLine border, corner radius 10"""

    def __init__(self, radius=10, bg="Glass", line="GlassLine"):
        super().__init__()
        self.radius, self.bg, self.line = radius, bg, line
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5)
        p.fillPath(rounded(r, self.radius), C[self.bg] if isinstance(self.bg, str) else self.bg)
        p.setPen(QPen(C[self.line] if isinstance(self.line, str) else self.line, 1))
        p.drawPath(rounded(r, self.radius))


class Button(QAbstractButton):
    """Quiet / Primary / GoButton / LinkButton / Link / Caption, as App.xaml's templates"""
    KINDS = {
        # kind: (height, padding x, radius, font size, weight)
        "quiet": (40, 18, 8, 14, W_SEMIBOLD), "primary": (40, 20, 8, 14, W_SEMIBOLD), "go": (50, 34, 10, 16, W_BOLD),
        "link": (None, 0, 0, 13, W_SEMIBOLD), "toggle": (None, 0, 0, 13, QFont.Weight.Normal), "caption": (36, 0, 8, 12, QFont.Weight.Normal),
    }

    def __init__(self, text="", kind="quiet", icon=None, color=None, size=None):
        super().__init__()
        self._glow = None
        self.kind, self.icon, self.hover, self._color = kind, icon, False, color
        h, self.padx, self.radius, fs, fw = self.KINDS[kind]
        self.setFont(font(size or fs, fw))
        self.setText(text)
        self.setCursor(Qt.CursorShape.PointingHandCursor if kind != "caption" else Qt.CursorShape.ArrowCursor)
        self.setFocusPolicy(Qt.FocusPolicy.TabFocus if kind in ("quiet", "primary", "go") else Qt.FocusPolicy.NoFocus)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.fixed_h = h
        if kind == "caption":
            self.setFixedSize(46, 36)
        self._glow = None
        if kind == "primary":
            self._glow = glow(self, "#3D63E6", 22, 6, 0.55)
        elif kind == "go":
            self._glow = glow(self, "#5FB08A", 30, 8, 0.5)
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)

    def setText(self, t):
        super().setText(t)
        self.updateGeometry()
        self.update()

    def set_color(self, color):
        self._color = color
        self.update()

    def sizeHint(self):
        fm = self.fontMetrics()
        w = fm.horizontalAdvance(self.text())
        if self.icon == "play":
            w += 21
        if self.kind in ("link", "toggle"):
            return QSize(w + 1, fm.height() + 2)
        if self.kind == "caption":
            return QSize(46, 36)
        return QSize(w + 2 * self.padx + 2, self.fixed_h)

    def enterEvent(self, e):
        self.hover = True
        self.update()

    def leaveEvent(self, e):
        self.hover = False
        self.update()

    def changeEvent(self, e):
        if self._glow is not None:
            self._glow.setEnabled(self.isEnabled())
        super().changeEvent(e)

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setRenderHint(QPainter.RenderHint.TextAntialiasing)
        r = QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5)
        k, on, hov, focus = self.kind, self.isEnabled(), self.hover and self.isEnabled(), self.hasFocus()
        ink = C["Ink"]
        if k == "quiet":
            bg = C["GlassHover"] if hov else argb("#08FFFFFF")
            line = C["BrandHover"] if focus else C["GlassLineStrong"] if hov else argb("#1FFFFFFF")
            p.fillPath(rounded(r, 8), bg)
            p.setPen(QPen(line, 1))
            p.drawPath(rounded(r, 8))
            ink = C["Ink"] if on else C["InkFaint"]
        elif k == "primary":
            bg = (C["BrandHover"] if hov else C["Brand"]) if on else argb("#14FFFFFF")
            p.fillPath(rounded(r, 8), bg)
            p.setPen(QPen(argb("#AAFFFFFF") if focus else argb("#33FFFFFF"), 1))
            p.drawPath(rounded(r, 8))
            ink = C["White"] if on else C["InkFaint"]
        elif k == "go":
            p.fillPath(rounded(r, 10), C["GoHover"] if hov else C["Go"])
            p.setPen(QPen(argb("#40FFFFFF"), 1))
            p.drawPath(rounded(r, 10))
            ink = C["GoInk"]
        elif k == "link":
            ink = C["White"] if hov else (self._color or C["BrandInk"])
        elif k == "toggle":
            ink = C["Ink"] if hov else (self._color or C["InkSoft"])
        elif k == "caption":
            if self.hover:
                p.fillPath(rounded(r, 8), C["GlassHover"])
            ink = C["Ink"] if self.hover else argb("#80FFFFFF")
            p.setPen(QPen(ink, 1.2 if self.icon == "close" else 1))
            cx, cy = self.width() / 2, self.height() / 2
            if self.icon == "min":
                p.fillRect(QRectF(cx - 5, cy, 10, 1), ink)
            else:
                p.drawLine(QPointF(cx - 5, cy - 5), QPointF(cx + 5, cy + 5))
                p.drawLine(QPointF(cx + 5, cy - 5), QPointF(cx - 5, cy + 5))
            return
        p.setFont(self.font())
        p.setPen(ink)
        fm = self.fontMetrics()
        tw = fm.horizontalAdvance(self.text())
        if self.icon == "play":   # (the triangle M0,0 L11,7 L0,14, 10 px before the text)
            total = 11 + 10 + tw
            x0 = (self.width() - total) / 2
            tri = QPainterPath()
            cy = self.height() / 2
            tri.moveTo(x0, cy - 7)
            tri.lineTo(x0 + 11, cy)
            tri.lineTo(x0, cy + 7)
            tri.closeSubpath()
            p.fillPath(tri, ink)
            p.drawText(QRectF(x0 + 21, 0, tw + 2, self.height()), Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft, self.text())
            return
        align = Qt.AlignmentFlag.AlignCenter if k in ("quiet", "primary", "go") else Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter
        p.drawText(QRectF(self.rect()), align, self.text())


class Tick(QAbstractButton):
    """CheckBox Tick: an 18x18 box, radius 5, its mark when checked, the text in InkSoft 13"""

    def __init__(self, text, checked=False):
        super().__init__()
        self.setCheckable(True)
        self.setChecked(checked)
        self.setText(text)
        self.setFont(font(13))
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)

    def sizeHint(self):
        return QSize(26 + self.fontMetrics().horizontalAdvance(self.text()) + 2, max(18, self.fontMetrics().height()))

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        y = (self.height() - 18) / 2
        box = QRectF(0.75, y + 0.75, 16.5, 16.5)
        if self.isChecked():
            p.fillPath(rounded(box, 5), C["Brand"])
            p.setPen(QPen(C["Brand"], 1.5))
            p.drawPath(rounded(box, 5))
            p.setPen(QPen(C["White"], 2))
            path = QPainterPath(QPointF(2, y + 8))
            path.lineTo(6, y + 12)
            path.lineTo(14, y + 3)
            p.drawPath(path)
        else:
            p.setPen(QPen(argb("#40FFFFFF"), 1.5))
            p.drawPath(rounded(box, 5))
        p.setFont(self.font())
        p.setPen(C["InkSoft"])
        p.drawText(QRectF(26, 0, self.width() - 26, self.height()), Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft,
                   self.text())


class Bar(QWidget):
    """ProgressBar Bar: 6 high, track #12FFFFFF, the indicator in Foreground, both radius 3"""

    def __init__(self, color="Brand", width=None):
        super().__init__()
        self.value, self.color = 0.0, color
        self.setFixedHeight(6)
        if width:
            self.setFixedWidth(width)

    def set(self, v, color=None):
        self.value = max(0.0, min(1.0, v))
        if color:
            self.color = color
        self.update()

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect())
        p.fillPath(rounded(r, 3), argb("#12FFFFFF"))
        if self.value > 0:
            p.fillPath(rounded(QRectF(0, 0, r.width() * self.value, r.height()), 3), C[self.color])


class Chip(QLabel):
    """Border Chip: 26 high, radius 13, #0DFFFFFF with #14FFFFFF, 10 px each side"""

    def __init__(self, text, color, soft=None):
        super().__init__(text)
        self.setFont(font(12))
        self.soft = soft
        self.setStyleSheet("color: %s; background: transparent; padding: 0 10px;" % css(C[color]))
        self.setFixedHeight(26)

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5)
        p.fillPath(rounded(r, 13), C[self.soft] if self.soft else argb("#0DFFFFFF"))
        p.setPen(QPen(argb("#14FFFFFF"), 1))
        p.drawPath(rounded(r, 13))
        p.end()
        super().paintEvent(e)


class PathEdit(QLineEdit):
    """TextBox Path: mono 12 in InkSoft, Glass with GlassLine (Brand when focused), radius 8, 40 high"""

    def __init__(self):
        super().__init__()
        self.setFont(font(12, family=MONO))
        self.setFixedHeight(40)
        self.setStyleSheet(
            "QLineEdit { color: %s; background: %s; border: 1px solid %s; border-radius: 8px; padding: 0 11px;"
            " selection-background-color: %s; }"
            "QLineEdit:focus { border-color: %s; }" % (css(C["InkSoft"]), css(C["Glass"]), css(C["GlassLine"]), css(C["Brand"]),
                                                       css(C["Brand"])))


class _ItemDelegate(QStyledItemDelegate):
    """ComboBoxItem ChoiceItem: radius 6, 10/7 padding, RaisedHover under the mouse, BrandInk when chosen"""

    def __init__(self, combo):
        super().__init__(combo)
        self.combo = combo

    def sizeHint(self, opt, idx):
        return QSize(opt.rect.width(), 7 + 7 + opt.fontMetrics.height())

    def paint(self, p, opt, idx):
        from PySide6.QtWidgets import QStyle
        p.save()
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        if opt.state & QStyle.StateFlag.State_MouseOver or opt.state & QStyle.StateFlag.State_Selected:
            p.fillPath(rounded(QRectF(opt.rect), 6), C["RaisedHover"])
        p.setFont(font(13))
        p.setPen(C["BrandInk"] if idx.row() == self.combo.currentIndex() else C["Ink"])
        p.drawText(QRectF(opt.rect).adjusted(10, 0, -10, 0), Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft,
                   idx.data())
        p.restore()


class Choice(QComboBox):
    """ComboBox Choice: 40 high, #08FFFFFF with #1FFFFFFF (GlassHover under the mouse), radius 8, its chevron; the list in Raised
    with LineStrong, radius 8, padding 4"""

    def __init__(self):
        super().__init__()
        self.setFixedHeight(40)
        self.setFont(font(13))
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.hover = False
        v = QListView()
        v.setItemDelegate(_ItemDelegate(self))
        v.setMouseTracking(True)
        self.setView(v)
        self.setStyleSheet(
            "QComboBox { background: transparent; border: none; color: %s; }"
            "QComboBox::drop-down { width: 0; border: none; } QComboBox::down-arrow { image: none; }"
            "QComboBox QAbstractItemView { background: %s; border: 1px solid %s; border-radius: 8px; padding: 4px; outline: none;"
            " color: %s; }" % (css(C["Ink"]), css(C["Raised"]), css(C["LineStrong"]), css(C["Ink"])))
        self.view().window().setWindowFlags(Qt.WindowType.Popup | Qt.WindowType.FramelessWindowHint |
                                            Qt.WindowType.NoDropShadowWindowHint)
        self.view().window().setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)

    def enterEvent(self, e):
        self.hover = True
        self.update()

    def leaveEvent(self, e):
        self.hover = False
        self.update()

    def showPopup(self):
        super().showPopup()
        w = self.view().window()   # (4 px below the box, as WPF's Popup margin)
        w.move(w.x(), self.mapToGlobal(self.rect().bottomLeft()).y() + 4)

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5)
        p.fillPath(rounded(r, 8), C["GlassHover"] if self.hover else argb("#08FFFFFF"))
        p.setPen(QPen(C["Brand"], 2) if self.hasFocus() and self.property("focus_ring") else QPen(argb("#1FFFFFFF"), 1))
        p.drawPath(rounded(r, 8))
        p.setPen(QPen(C["InkSoft"], 1.3))
        x, y = self.width() - 12 - 8, self.height() / 2 - 2
        path = QPainterPath(QPointF(x, y))
        path.lineTo(x + 4, y + 4)
        path.lineTo(x + 8, y)
        p.drawPath(path)
        p.setFont(self.font())
        p.setPen(C["Ink"])
        p.drawText(QRectF(12, 0, self.width() - 40, self.height()), Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft,
                   self.currentText())


class Ring(QWidget):
    """The build's ring: a #14FFFFFF circle of 10, the Brand arc over it (round caps), the percent in the middle"""

    def __init__(self):
        super().__init__()
        self.setFixedSize(120, 120)
        self.f = 0.0

    def set(self, f):
        self.f = max(0.0, min(1.0, f))
        self.update()

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(QPen(argb("#14FFFFFF"), 10))
        p.drawEllipse(QRectF(5, 5, 110, 110))
        if self.f >= 0.002:
            pen = QPen(C["Brand"], 10)
            pen.setCapStyle(Qt.PenCapStyle.RoundCap)
            p.setPen(pen)
            p.drawArc(QRectF(5, 5, 110, 110), 90 * 16, -int(self.f * 360 * 16))
        p.setPen(C["Ink"])
        p.setFont(font(24, W_BOLD))
        p.drawText(QRectF(self.rect()), Qt.AlignmentFlag.AlignCenter, "%d%%" % int(self.f * 100))


def tick_path(cx, cy):
    """M0,4 L3.5,7.5 L10,0, centred on (cx, cy)"""
    path = QPainterPath(QPointF(cx - 5, cy))
    path.lineTo(cx - 1.5, cy + 3.5)
    path.lineTo(cx + 5, cy - 4)
    return path
