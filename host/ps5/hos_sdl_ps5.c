/* PS5: host/hos_sdl.h on the console instead of SDL. The window is the console's one display (VK_KHR_display, as
   PS5_VulkanTemplate's titles make it: skills/ps5-homebrew/references/vulkan-on-radv.md), the controller is the
   DualSense of the user who started the title (scePad, host/ps5/platform.c), and the sound is the system user's main
   port (sceAudioOut, 48 kHz stereo 16-bit). Everything that reads these (host/posix/display_sdl.c, host/pad_sdl.c,
   win32_posix.c's waveOut) is the Linux build's, unchanged. No keyboard, mouse or microphone on the console here.

   HOS_PS5_DISPLAY=<width>x<height>: the display mode (3840x2160, 2560x1440 or 1920x1080; default 1920x1080, the size
   the renderer's default output 2048x1152 is closest to). The 59.94 Hz mode of that size is taken: the game paces to
   60. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hos.h"
#include "../hos_sdl.h"
#include "platform.h"

PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);   /* (RADV, linked in) */

int hos_sdl_load(void) { return 3; }   /* (SDL3's numbering for everything below) */
int hos_sdl_init(uint32_t subsystems) {
    static int pads;
    if ((subsystems & HOS_SDL_GAMEPAD) && !pads) pads = 1, pad_open();
    return 1;
}
void hos_sdl_hint(const char *name, const char *value) { (void)name, (void)value; }
const char *hos_sdl_error(void) { return "not on the console"; }
void hos_sdl_identity(const char *name, const char *app_id) { (void)name, (void)app_id; }

/* ---- the display ---- */
static int mode_w = 1920, mode_h = 1080, mode_mhz = 59940;
static void mode_wanted(void) {
    static int done;
    if (done) return;
    done = 1;
    const char *e = getenv("HOS_PS5_DISPLAY");
    int w = 0, h = 0;
    if (e && sscanf(e, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) mode_w = w, mode_h = h;
}
static int window_token;
void *hos_sdl_window(const char *title, int width, int height) {
    (void)title, (void)width, (void)height;
    mode_wanted();
    return &window_token;
}
void hos_sdl_fullscreen(void *window, int on) { (void)window, (void)on; }
void hos_sdl_relative_mouse(void *window, int on) { (void)window, (void)on; }
int hos_sdl_desktop_size(int *width, int *height) {
    mode_wanted();
    *width = mode_w, *height = mode_h;
    return 1;
}
int hos_sdl_refresh_mhz(void *window) { (void)window; return mode_mhz; }
void hos_sdl_pixel_size(void *window, int *width, int *height) { (void)window; *width = mode_w, *height = mode_h; }
void hos_sdl_window_size(void *window, int *width, int *height) { (void)window; *width = mode_w, *height = mode_h; }
void hos_sdl_icon(void *window, const char *png) { (void)window, (void)png; }

/* The plane surface of the display mode of the wanted size, 59.94 Hz where the display offers it (the modes come
   largest first, each size's fastest first) */
int hos_sdl_vulkan_surface(void *window, void *instance_p, void *surface_out) {
    (void)window;
    mode_wanted();
    VkInstance instance = (VkInstance)instance_p;
#define GET(name) PFN_##name name = (PFN_##name)vk_icdGetInstanceProcAddr(instance, #name)
    GET(vkEnumeratePhysicalDevices);
    GET(vkGetPhysicalDeviceDisplayPropertiesKHR);
    GET(vkGetDisplayModePropertiesKHR);
    GET(vkCreateDisplayPlaneSurfaceKHR);
#undef GET
    if (!vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceDisplayPropertiesKHR || !vkGetDisplayModePropertiesKHR ||
        !vkCreateDisplayPlaneSurfaceKHR) {
        hos_log("ps5: the instance has no VK_KHR_display");
        return 0;
    }
    uint32_t n = 1;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    if (vkEnumeratePhysicalDevices(instance, &n, &gpu) < 0 || !n) return 0;
    VkDisplayPropertiesKHR display;
    n = 1;
    if (vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &n, &display) < 0 || !n) {
        hos_log("ps5: no display");
        return 0;
    }
    VkDisplayModePropertiesKHR modes[16];
    n = 16;
    if (vkGetDisplayModePropertiesKHR(gpu, display.display, &n, modes) < 0 || !n) return 0;
    int pick = -1;
    for (uint32_t i = 0; i < n; i++) {
        const VkDisplayModeParametersKHR *p = &modes[i].parameters;
        hos_log("ps5: display mode %ux%u at %u mHz", p->visibleRegion.width, p->visibleRegion.height, p->refreshRate);
        if ((int)p->visibleRegion.width != mode_w || (int)p->visibleRegion.height != mode_h) continue;
        if (pick < 0 || (p->refreshRate < 61000 && modes[pick].parameters.refreshRate >= 61000)) pick = (int)i;
    }
    if (pick < 0) {
        hos_log("ps5: no %dx%d mode: the largest instead", mode_w, mode_h);
        pick = 0;
    }
    const VkDisplayModeParametersKHR *p = &modes[pick].parameters;
    mode_w = (int)p->visibleRegion.width, mode_h = (int)p->visibleRegion.height, mode_mhz = (int)p->refreshRate;
    VkDisplaySurfaceCreateInfoKHR ci = {VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
    ci.displayMode = modes[pick].displayMode;
    ci.planeIndex = 0;
    ci.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    ci.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    ci.globalAlpha = 1.0f;
    ci.imageExtent = p->visibleRegion;
    const VkResult r = vkCreateDisplayPlaneSurfaceKHR(instance, &ci, NULL, (VkSurfaceKHR *)surface_out);
    hos_log("ps5: display %dx%d at %d mHz: surface %d", mode_w, mode_h, mode_mhz, (int)r);
    return r == VK_SUCCESS;
}

/* ---- keyboard, mouse and events: none on the console ---- */
const uint8_t *hos_sdl_keys(int *count) {
    static const uint8_t none[512];
    *count = 512;
    return none;
}
uint32_t hos_sdl_mouse_buttons(void) { return 0; }
int hos_sdl_poll_event(HosSdlEvent *e) { (void)e; return 0; }
int hos_sdl_wait_event(HosSdlEvent *e, int timeout_ms) {
    (void)e;
    if (timeout_ms > 0) {
        struct timespec t = {timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000000L};
        nanosleep(&t, NULL);
    }
    return 0;
}

/* ---- sound: the platform's thread asks for 256 frames at a time; the game's stream is pulled in its own format and
   brought to 48 kHz stereo 16-bit (the first two channels; the game is asked for stereo: hos_sdl_audio_channels) ---- */
static void (*audio_pull)(void *user, uint8_t *out, int len);
static void *audio_user;
static int audio_channels, audio_s16;
static void audio_fill(int16_t *frames, int count, void *user) {
    (void)user;
    static uint8_t buf[256 * 8 * 4];
    const int frame_bytes = audio_channels * (audio_s16 ? 2 : 4);
    if (count > 256) count = 256;
    audio_pull(audio_user, buf, count * frame_bytes);
    for (int i = 0; i < count; i++)
        for (int c = 0; c < 2; c++) {
            const int from = c < audio_channels ? c : 0;
            if (audio_s16) {
                frames[i * 2 + c] = ((const int16_t *)buf)[i * audio_channels + from];
            } else {
                float v = ((const float *)buf)[i * audio_channels + from] * 32767.0f;
                frames[i * 2 + c] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
            }
        }
}
int hos_sdl_audio_open(int freq, int channels, int s16, void (*pull)(void *user, uint8_t *out, int len), void *user) {
    if (freq != 48000) hos_log("ps5: the game's sound is %d Hz, played as 48000", freq);
    if (channels < 1 || channels > 8) return 0;
    audio_pull = pull, audio_user = user, audio_channels = channels, audio_s16 = s16;
    return audio_start(audio_fill, NULL);
}
int hos_sdl_audio_channels(void) { return 2; }

/* ---- microphones: none ---- */
int hos_sdl_mic_list(uint32_t *ids, char (*names)[128], int max) { (void)ids, (void)names, (void)max; return 0; }
void *hos_sdl_mic_open(uint32_t id, int freq, int channels, uint16_t sdl_format) {
    (void)id, (void)freq, (void)channels, (void)sdl_format;
    return NULL;
}
int hos_sdl_mic_available(void *mic) { (void)mic; return 0; }
int hos_sdl_mic_read(void *mic, void *dst, int bytes) { (void)mic, (void)dst, (void)bytes; return 0; }
void hos_sdl_mic_clear(void *mic) { (void)mic; }
void hos_sdl_mic_close(void *mic) { (void)mic; }

/* ---- the controller: player 0's DualSense, as SDL3 numbers a gamepad's buttons and axes ---- */
static struct pad pad_now;
static int pad_token;
int hos_sdl_pads_list(int32_t *ids, int *opens, int max) {
    if (max < 1 || !(pad_players() & 1)) return 0;
    ids[0] = 1, opens[0] = 0;
    return 1;
}
void *hos_sdl_pad_open(int what) { (void)what; return &pad_token; }
void hos_sdl_pad_close(void *pad) { (void)pad; }
int hos_sdl_pad_connected(void *pad) { (void)pad; return (pad_players() & 1) != 0; }
void hos_sdl_pads_update(void) { pad_poll(&pad_now); }
int hos_sdl_pad_button(void *pad, int button) {
    (void)pad;
    /* SOUTH EAST WEST NORTH BACK GUIDE START LSTICK RSTICK LSHOULDER RSHOULDER UP DOWN LEFT RIGHT: the touch pad's click
       is Back (the Switch's minus), the PS button stays the system's */
    static const uint32_t map[15] = {PAD_CROSS, PAD_CIRCLE, PAD_SQUARE, PAD_TRIANGLE, PAD_TOUCH_PAD, 0, PAD_OPTIONS,
                                     PAD_L3, PAD_R3, PAD_L1, PAD_R1, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT};
    return button >= 0 && button < 15 && map[button] && (pad_now.held & map[button]) != 0;
}
int16_t hos_sdl_pad_axis(void *pad, int axis) {
    (void)pad;
    const float v = axis == 0 ? pad_now.left_x : axis == 1 ? pad_now.left_y : axis == 2 ? pad_now.right_x
                  : axis == 3 ? pad_now.right_y : axis == 4 ? pad_now.l2 : axis == 5 ? pad_now.r2 : 0.0f;
    const float s = v * 32767.0f;   /* (y down-positive, as SDL's) */
    return (int16_t)(s > 32767.0f ? 32767 : s < -32768.0f ? -32768 : s);
}
const char *hos_sdl_pad_name(void *pad) { (void)pad; return "DualSense"; }
int32_t hos_sdl_pad_id(void *pad) { (void)pad; return 1; }
void hos_sdl_pad_rumble(void *pad, uint16_t low, uint16_t high, uint32_t ms) {
    (void)pad, (void)ms;
    pad_vibrate(low / 65535.0f, high / 65535.0f);
}
int hos_sdl_pad_type(void *pad) { (void)pad; return 0; }
int hos_sdl_pad_has_effect(void) { return 0; }
int hos_sdl_pad_effect(void *pad, const void *data, int size) { (void)pad, (void)data, (void)size; return 0; }
/* shortcut: no motion yet (scePadReadState's sensors); the game sees a pad lying still */
int hos_sdl_pad_has_sensor(void *pad, int type) { (void)pad, (void)type; return 0; }
int hos_sdl_pad_sensor_enable(void *pad, int type, int on) { (void)pad, (void)type, (void)on; return 0; }
int hos_sdl_pad_sensor(void *pad, int type, float *data, int n) { (void)pad, (void)type, (void)data, (void)n; return 0; }
