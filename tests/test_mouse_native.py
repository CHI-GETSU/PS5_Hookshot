"""Native mouse look turns the player camera's own angles (host/mouse_native.c).

The file is compiled on its own against a fake guest memory that holds one player camera: its pitch limits at +0x178, its
pitch at +0x18c and its yaw at +0x1a4. hos_mouse_camera is what the camera's update (f_80644c80, nx2/oot_patches.py) calls
each frame. Until it has run the mouse's motion is the right stick's (hos_mouse_native gives -1); then it turns yaw and
pitch by 0.1 degrees a count at sensitivity 1, pitch kept within the limits and yaw wrapped at 180, up and down swapped
with the menu's Invert mouse vertical (option 8), and the controller reads leave the motion to the camera. Without the
menu's Mouse camera (option 6), or on an object that is not a camera, it does nothing; 150 ms without the camera (the
pause menu, another camera) give the motion back to the stick. The camera takes the mouse only in its states 3 and 4 (at
+0x204: following Link, turned by the player): aiming in first person leaves it updating with state 0 while another
camera draws, and the motion is then the stick's.
python -I -B tests/test_mouse_native.py   (Linux: host/posix's windows.h)
"""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

HOS_H = r"""
#pragma once
#include <stdint.h>
#include <stddef.h>
extern uint8_t *fake_heap;
#define HOS_HEAP_BASE 0x1000000000ULL
#define GP(a) ((void *)(fake_heap + ((uint64_t)(a) - HOS_HEAP_BASE)))
extern uint64_t hos_heap_size;
void hos_log(const char *fmt, ...);
int64_t hos_now_ns(void);
"""

FIXTURE = r"""
#include "hos.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
uint8_t *fake_heap;
uint64_t hos_heap_size = 64 * 4096;
double hos_mouse_sens = 1.0;
static int64_t clock_ns = 1000000000LL;
void hos_log(const char *fmt, ...) { va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); fputc(10, stderr); }
int64_t hos_now_ns(void) { return clock_ns; }
static int32_t move_x, move_y;
static int captured = 1, menu_invert, menu_native = 1;
int rx_option(int id, int otherwise) { return id == 8 ? menu_invert : id == 6 ? menu_native : otherwise; }
int hos_mouse_take(int32_t *dx, int32_t *dy) { *dx = move_x, *dy = move_y; move_x = move_y = 0; return captured; }
void hos_mouse_give_back(int32_t dx, int32_t dy) { move_x += dx, move_y += dy; }
int hos_mouse_native(void);
void hos_mouse_camera(uint64_t camera);
static const uint64_t CAM = HOS_HEAP_BASE + 10 * 4096 + 0x40, OTHER = HOS_HEAP_BASE + 20 * 4096;
static float getf(uint64_t a) { float f; memcpy(&f, GP(a), 4); return f; }
static void putf(uint64_t a, float f) { memcpy(GP(a), &f, 4); }
static int fails;
#define PITCH (CAM + 0x18c)
#define YAW (CAM + 0x1a4)
static void state(uint32_t v) { memcpy(GP(CAM + 0x204), &v, 4); }
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s (pitch %g yaw %g)\n", __LINE__, #c, getf(PITCH), getf(YAW)); fails++; } } while (0)
static int near(float a, float b) { return fabsf(a - b) < 1e-3f; }
static void frame(int32_t dx, int32_t dy) { move_x += dx, move_y += dy; clock_ns += 16666667; hos_mouse_camera(CAM); }
int main(void) {
    fake_heap = (uint8_t *)calloc(64, 4096);
    putf(CAM + 0x178, -85), putf(CAM + 0x17c, 89);
    putf(PITCH, 10), putf(YAW, 30);
    state(4);   /* turned by the player */
    move_x = 5, move_y = 3;
    CHECK(hos_mouse_native() == -1);   /* no camera has run yet: the stick takes it */
    move_x = move_y = 0;
    frame(10, 4);
    CHECK(near(getf(YAW), 29.0f) && near(getf(PITCH), 10.4f));   /* right: yaw falls; down: pitch rises */
    move_x = 3;
    CHECK(hos_mouse_native() == 1 && move_x == 3);   /* the camera's now: the motion stays for it */
    frame(-3, 0);
    frame(0, 100000);
    CHECK(near(getf(PITCH), 89.0f));   /* kept within the limits */
    frame(0, -100000);
    CHECK(near(getf(PITCH), -85.0f));
    putf(YAW, -179.5f);
    frame(10, 0);
    CHECK(near(getf(YAW), 179.5f));   /* wrapped */
    putf(PITCH, 0);
    menu_invert = 1;   /* the menu's Invert mouse vertical: down tilts up */
    frame(0, 10);
    CHECK(near(getf(PITCH), -1.0f));
    menu_invert = 0;
    putf(OTHER + 0x18c, NAN);   /* (no limits, no angles: not a camera) */
    move_x = 50;
    hos_mouse_camera(OTHER);
    CHECK(move_x == 50 && isnan(getf(OTHER + 0x18c)));
    move_x = 0;
    menu_native = 0;   /* the menu's Mouse camera off: the hook does nothing */
    putf(YAW, 0);
    frame(10, 0);
    CHECK(near(getf(YAW), 0.0f));
    move_x = 0;
    menu_native = 1;
    frame(0, 0);
    captured = 0;
    CHECK(hos_mouse_native() == 0);   /* not captured: nothing */
    captured = 1;
    clock_ns += 200000000LL;   /* no camera for 200 ms (the pause menu): the stick again */
    CHECK(hos_mouse_native() == -1);
    /* aiming in first person (the slingshot): the camera keeps updating, state 0, another draws: the stick takes it */
    state(4);
    frame(0, 0);   /* the camera's a moment ago (the hook ran: hos_mouse_native would give 1 for 150 ms) */
    state(0);
    putf(YAW, 0);
    frame(10, 0);
    CHECK(near(getf(YAW), 0.0f) && move_x == 10);
    CHECK(hos_mouse_native() == -1);   /* at once, not 150 ms later */
    move_x = 0;
    state(3);   /* following Link again: the mouse turns it */
    frame(10, 0);
    CHECK(near(getf(YAW), -1.0f));
    CHECK(hos_mouse_native() == 1);
    if (!fails) printf("PASS\n");
    return fails != 0;
}
"""


class MouseNativeTests(unittest.TestCase):
    def test_turns_the_player_camera(self):
        if sys.platform == "win32":
            self.skipTest("host/posix's windows.h stands in for Windows here")
        compiler = shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            self.skipTest("no C compiler")
        with tempfile.TemporaryDirectory(prefix="hookshot-mouse-native-") as tmp:
            tmp = Path(tmp)
            (tmp / "hos.h").write_text(HOS_H, encoding="utf-8")
            # (beside the stand-in hos.h, so its #include "hos.h" finds that one)
            shutil.copy(ROOT / "host" / "mouse_native.c", tmp / "mouse_native.c")
            (tmp / "t.c").write_text(FIXTURE, encoding="utf-8")
            binary = tmp / "t"
            build = subprocess.run([compiler, "-std=gnu11", "-Wall", "-O1", "-g", "-I", str(tmp), "-I", str(ROOT / "host" / "posix"),
                                    str(tmp / "t.c"), str(tmp / "mouse_native.c"), "-o", str(binary), "-lm"],
                                   text=True, capture_output=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], text=True, capture_output=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    raise SystemExit(unittest.main(verbosity=1))
