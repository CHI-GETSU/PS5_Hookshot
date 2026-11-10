"""PS5: the game, the runtime and the Vulkan back end compiled for the console and linked into one title, dist/PPSA99024.

    python3 -I ps5/build.py [--jobs N] [--compile-only]

What it builds from (docs/PS5.md has the whole route):
  - the lifted game: work/lift/sa/gen (nx2/lift.py --standalone --write, from your own dump; never committed);
  - host/ as the Linux build compiles it (host/posix's Win32 calls), with host/ps5 in place of SDL, and the PS5's
    memory, threads and modules (host/ps5/win32_ps5.c);
  - gpu/vk with Eden's shader recompiler, linked in rather than loaded (no dynamic loading on the console);
  - Mesa's RADV from PS5_Vulkan (its release archive and link recipe), the payload SDK fork's platform layer.

Paths: OOT_WORK (default work/lift), PS5_PAYLOAD_SDK (default ../PS5_VulkanTemplate's pinned SDK), PS5_VULKAN
(default ../PS5_Vulkan). Objects are kept by a key of their flags, source and headers: a rebuild compiles what changed.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "nx2"))
os.environ.setdefault("OOT_WORK", os.path.join(ROOT, "work", "lift"))
import kit       # noqa: E402  (nx2: the lift's paths)
import rtgen     # noqa: E402  (the 64-bit guest window's runtime header)
import logo_rgba  # noqa: E402
import ui_fonts   # noqa: E402  (the overlays' bundled fonts, compiled in)
import version   # noqa: E402

SDK = os.environ.get("PS5_PAYLOAD_SDK", os.path.join(ROOT, "..", "PS5_VulkanTemplate", ".deps", "native", "ps5-payload-sdk"))
VULKAN = os.environ.get("PS5_VULKAN", os.path.join(ROOT, "..", "PS5_Vulkan"))
DEPS = os.path.join(ROOT, ".local", "ps5-deps")
CPM = os.path.join(DEPS, "cpm")
FFMPEG = os.path.join(DEPS, "ffmpeg")
GEN = os.path.join(kit.WORK, "sa", "gen")
OUT = os.path.join(ROOT, "work", "ps5")
HOST, POSIX, PS5 = (os.path.join(ROOT, "host", d) for d in ("", "posix", "ps5"))
VK = os.path.join(ROOT, "gpu", "vk")
OPUS = os.path.join(ROOT, "third_party", "opus")
# PS5_MihawkGen (github.com/mihawk-99/PS5_MihawkGen, GPL-3.0-or-later), checked out beside this repository: its generator
# (framegen/src) is compiled in and its trained network goes beside the game, as PS5_RetroArch's build has them. Absent:
# the renderer's own frame interpolation.
MIHAWKGEN = os.environ.get("PS5_MIHAWKGEN", os.path.join(ROOT, "..", "PS5_MihawkGen"))
MIHAWKGEN_SOURCES = [os.path.join(MIHAWKGEN, "framegen", "src", n + ".cpp")
                     for n in ("fg_common", "fg_vulkan", "fg_neural", "fg_splat")]
MIHAWKGEN_NETWORK = os.path.join(MIHAWKGEN, "framegen", "models", "network.fgw")
HAS_MIHAWKGEN = all(os.path.exists(f) for f in MIHAWKGEN_SOURCES + [MIHAWKGEN_NETWORK])

# The guest window at 64 GiB (host/ps5/win32_ps5.h); Zen 2; the SDK wrapper adds the console's target and its
# required flags (PS5_Vulkan tooling/prospero-clang18). Frame pointers: console backtraces walk them.
COMMON = ["-march=znver2", "-ffp-contract=off", "-DA64_HOST_BASE=0x1000000000ULL", "-fno-omit-frame-pointer",
          "-ffunction-sections", "-fdata-sections", "-g0", "-Wno-everything"]


def pkg(name, version_):
    return os.path.join(CPM, name, version_)


def cc_wrapper(cxx=False):
    return ["sh", os.path.join(VULKAN, "tooling", "prospero-clang18")] + (["-x", "c++"] if cxx else [])


def tree_digest(*dirs, ext=(".h", ".hpp", ".inc")):
    h = hashlib.sha1()
    for d in dirs:
        for base, _, files in sorted(os.walk(d)):
            for f in sorted(files):
                if f.endswith(ext):
                    p = os.path.join(base, f)
                    h.update(p.encode())
                    with open(p, "rb") as fh:
                        h.update(fh.read())
    return h.hexdigest()


def jobs_for(rt64, gen_dir):
    """(source, object, flags, header digest) of everything the title links"""
    out = []
    game_digest = tree_digest(GEN, rt64, os.path.join(kit.KIT, "rt"), os.path.join(ROOT, "nx2", "rt"))
    game_fl = COMMON + ["-I" + rt64, "-I" + GEN, "-I" + os.path.join(kit.KIT, "rt"), "-I" + os.path.join(ROOT, "nx2", "rt"),
                        "-fno-strict-aliasing", "-mcx16", "-O1", "-mllvm", "-rotation-max-header-size=0", "-mllvm",
                        "-capture-tracking-max-uses-to-explore=5000", "-DA64_POLLING", "-DA64_WRITE_TRACK"]
    chunks = sorted(f for f in os.listdir(GEN) if f.startswith("code_") and f.endswith(".c"))
    game = [os.path.join(GEN, c) for c in chunks] + \
        [os.path.join(GEN, f) for f in ("a64_table.c", "a64_hooks.c", "a64_suyu.c", "a64_stubs.c", "hos_image.c", "nvn2_exports.c")] + \
        [os.path.join(rt64, "a64_rt.c")]
    out += [(s, os.path.join(OUT, "obj", "game_" + os.path.basename(s)[:-2] + ".o"), game_fl, game_digest) for s in game]

    # the runtime: host/ as Linux builds it, without SDL's loader (host/ps5/hos_sdl_ps5.c) and the Linux-only files
    host_digest = tree_digest(HOST, rt64, GEN) + tree_digest(os.path.join(VK), ext=("vk_api.h", "dlss_math.h"))
    host_fl = COMMON + ["-I" + POSIX, "-D_GNU_SOURCE", "-I" + HOST, "-I" + rt64, "-I" + GEN, "-O2", "-mcx16", "-DA64_POLLING",
                        "-DA64_WRITE_TRACK", "-I" + os.path.join(OPUS, "include"), "-I" + os.path.join(FFMPEG, "include"),
                        "-I" + os.path.join(DEPS, "zstd", "include"),
                        "-I" + os.path.join(pkg("vulkanheaders", "v1.4.345"), "include")]
    skip = {"display.c", "mf_av1.c", "rx_nowgpu.c", "pgo_rt.c", "hos_sdl.c", "mingw_msvc_shims.c", "msvc_oldnames.c"}
    host = [os.path.join(HOST, f) for f in sorted(os.listdir(HOST)) if f.endswith(".c") and f not in skip] + \
        [os.path.join(POSIX, f) for f in ("win32_posix.c", "display_sdl.c", "linux_stubs.c")] + \
        [os.path.join(PS5, f) for f in sorted(os.listdir(PS5)) if f.endswith(".c")]
    stamp = version.define(ROOT)
    for s in host:
        rel = os.path.relpath(s, HOST).replace(os.sep, "_")[:-2]
        fl = host_fl + ([stamp] if os.path.basename(s) == "hos_main.c" else [])
        out.append((s, os.path.join(OUT, "obj", "host_" + rel + ".o"), fl, host_digest))

    # libopus (nn::codec's Opus decoder)
    ofl = COMMON + ["-O2", "-DOPUS_BUILD", "-DUSE_ALLOCA", "-DHAVE_LRINTF", "-DHAVE_LRINT"] + \
        ["-I" + os.path.join(OPUS, d) for d in ("include", "celt", "silk", "silk/float", "src")]
    for rel in open(os.path.join(OPUS, "sources.txt")).read().split():
        out.append((os.path.join(OPUS, rel), os.path.join(OUT, "obj", "opus_" + rel.replace("/", "_")[:-2] + ".o"), ofl,
                    "opus-1.5.2"))

    # the Vulkan back end, Eden's recompiler and sirit, as nx2/build.py's vk_sources (C++20 with exceptions: Eden throws)
    import build as upstream   # (nx2/build.py: the source list, kept in one place)
    spirv = pkg("spirv-headers", "vulkan-sdk-1.4.341.0")
    vfl = COMMON + ["-I" + POSIX, "-D_GNU_SOURCE", "-include", os.path.join(PS5, "win32_ps5.h"),
                    "-include", "algorithm", "-include", "cstdio", "-include", "cstdlib", "-include", "cstring",
                    "-include", "functional", "-include", "string", "-include", "vector",
                    "-std=c++20", "-O2", "-DNDEBUG", "-fexceptions", "-fcxx-exceptions", "-DVK_NO_PROTOTYPES",
                    "-DMKWVK_BUILDING", "-DHOOKSHOT_RELEASE", "-DMKWVK_INCLUDES=2",
                    "-I" + os.path.join(VK, "compat"), "-I" + os.path.join(ROOT, "gpu", "compat"), "-I" + os.path.join(VK, "eden"),
                    "-I" + os.path.join(ROOT, "gpu", "third_party", "suyu"),
                    "-I" + os.path.join(VK, "sirit", "include"), "-I" + os.path.join(VK, "sirit", "src"),
                    "-I" + os.path.join(spirv, "include"), "-I" + os.path.join(spirv, "include", "spirv", "unified1"),
                    "-I" + os.path.join(pkg("vulkanheaders", "v1.4.345"), "include"), "-I" + VK, "-I" + gen_dir,
                    "-I" + os.path.join(pkg("vulkanmemoryallocator", "v3.3.0"), "include")]
    translator = hashlib.sha1()
    for base, _, files in sorted(os.walk(VK)):
        if os.path.relpath(base, VK).split(os.sep)[0] in ("eden", "sirit", "compat"):
            for f in sorted(files):
                with open(os.path.join(base, f), "rb") as fh:
                    translator.update(f.encode() + fh.read())
    for n in ("vk_shader.cpp", "vk_bank.cpp", "vk_state.h", "vk_api.h"):
        with open(os.path.join(VK, n), "rb") as fh:
            translator.update(fh.read())
    shader_stamp = "-DHOOKSHOT_SHADER_STAMP=0x%sull" % translator.hexdigest()[:16]
    lib_digest = tree_digest(os.path.join(VK, "eden"), os.path.join(VK, "sirit"), os.path.join(VK, "compat"))
    vk_digest = tree_digest(VK, os.path.join(ROOT, "gpu", "compat"), os.path.join(ROOT, "gpu", "render"), POSIX, PS5,
                            os.path.join(ROOT, "gpu", "third_party", "suyu", "video_core", "textures"))
    # Dear ImGui (the menu and the overlays: vk_imgui.cpp, vk_menu.cpp), as nx2/build.py compiles it; the game's font files
    # are zstd frames, read with the console's zstd (ps5/tools/build-zstd.sh), not third_party/zstd's second copy
    imgui = os.path.join(ROOT, "third_party", "imgui")
    imgui_fl = ["-I" + imgui, "-I" + os.path.join(DEPS, "zstd", "include"), "-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES",
                "-DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS", "-DIMGUI_DISABLE_WIN32_FUNCTIONS", "-DIMGUI_DISABLE_DEMO_WINDOWS",
                "-DIMGUI_DISABLE_DEBUG_TOOLS"]
    imgui_digest = tree_digest(imgui, os.path.join(DEPS, "zstd", "include"))
    for f in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp", "backends/imgui_impl_vulkan.cpp"):
        out.append((os.path.join(imgui, *f.split("/")), os.path.join(OUT, "obj", "vk_imgui_" + os.path.basename(f)[:-4] + ".o"),
                    vfl + imgui_fl, imgui_digest))
    out.append((os.path.join(gen_dir, "ui_fonts.cpp"), os.path.join(OUT, "obj", "vk_ui_fonts.o"), COMMON + ["-O0", "-w"], ""))
    for s in upstream.vk_sources():
        if os.path.basename(s) == "vk_ngx_msvc_rt.cpp":
            continue
        rel = os.path.relpath(s, os.path.join(ROOT, "gpu")).replace(os.sep, "_")[:-4]
        lib = os.path.relpath(s, VK).split(os.sep)[0] in ("eden", "sirit")
        fl = vfl + ([shader_stamp] if os.path.basename(s) == "vk_shader.cpp" else []) + \
            ([stamp] if os.path.basename(s) == "vk_menu.cpp" else []) + \
            (imgui_fl if os.path.basename(s) in ("vk_imgui.cpp", "vk_menu.cpp") else []) + \
            (["-DHOOKSHOT_MIHAWKGEN", "-I" + os.path.join(MIHAWKGEN, "framegen", "include")]
             if HAS_MIHAWKGEN and os.path.basename(s) == "vk_framegen.cpp" else [])
        out.append((s, os.path.join(OUT, "obj", "vk_" + rel + ".o"), fl,
                    lib_digest if lib else vk_digest + (imgui_digest if os.path.basename(s) in ("vk_imgui.cpp", "vk_menu.cpp") else "")))
    if HAS_MIHAWKGEN:   # (its Vulkan calls through the back end's loaded pointers: vk_loader.h, as every unit of gpu/vk)
        mfl = vfl + ["-include", os.path.join(VK, "vk_loader.h"), "-I" + os.path.join(MIHAWKGEN, "framegen", "include"),
                     "-I" + os.path.join(MIHAWKGEN, "framegen", "src")]
        mdigest = tree_digest(os.path.join(MIHAWKGEN, "framegen")) + vk_digest
        for s in MIHAWKGEN_SOURCES:
            out.append((s, os.path.join(OUT, "obj", "mihawkgen_" + os.path.basename(s)[:-4] + ".o"), mfl, mdigest))
    return out


def key_of(src, flags, digest):
    h = hashlib.sha1(" ".join(flags).encode() + digest.encode())
    with open(src, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def compile_one(job):
    src, obj, flags, digest = job
    key = key_of(src, flags, digest)
    stamp = obj + ".key"
    if os.path.exists(obj) and os.path.exists(stamp) and open(stamp).read() == key:
        return src, None, 0.0
    cxx = src.endswith(".cpp")
    t = time.time()
    r = subprocess.run(cc_wrapper(cxx) + flags + ["-c", src, "-o", obj], capture_output=True, text=True,
                       env=dict(os.environ, PS5_PAYLOAD_SDK=SDK, PS5_CLANG=os.environ.get("PS5_CLANG") or shutil.which("clang")))
    if r.returncode:
        return src, (r.stdout + r.stderr)[-4000:], time.time() - t
    with open(stamp, "w") as f:
        f.write(key)
    return src, None, time.time() - t


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    p.add_argument("--compile-only", action="store_true")
    a = p.parse_args()
    for need, what in ((os.path.join(GEN, "a64_table.c"), "the lifted game (nx2/lift.py --standalone --write)"),
                       (os.path.join(SDK, "bin", "prospero-lld"), "the PS5 payload SDK (PS5_PAYLOAD_SDK)"),
                       (os.path.join(VULKAN, "tools", "radv-link.sh"), "PS5_Vulkan (PS5_VULKAN)"),
                       (os.path.join(FFMPEG, "include", "libavcodec", "avcodec.h"), "FFmpeg (ps5/tools/build-ffmpeg.sh)"),
                       (os.path.join(DEPS, "zstd", "lib", "libzstd.a"), "zstd (ps5/tools/build-zstd.sh)"),
                       (pkg("vulkanheaders", "v1.4.345"), "the Vulkan headers (ps5/build-title.sh fetches them)")):
        if not os.path.exists(need):
            sys.exit("ps5/build.py: missing %s: %s" % (what, need))
    os.makedirs(os.path.join(OUT, "obj"), exist_ok=True)
    rt64 = rtgen.write(os.path.join(OUT, "rt64"))
    gen_dir = os.path.join(OUT, "gen")
    logo_rgba.write(os.path.join(gen_dir, "hookshot_logo.h"))
    ui_fonts.write(os.path.join(gen_dir, "ui_fonts.cpp"))
    jobs = jobs_for(rt64, gen_dir)
    # the longest first (the lifted chunks), so no long compile is left alone at the end
    jobs.sort(key=lambda j: -os.path.getsize(j[0]))
    print("ps5: %d files, %d jobs" % (len(jobs), a.jobs), flush=True)
    failed, built, t0 = [], 0, time.time()
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        for src, err, secs in pool.map(compile_one, jobs):
            if err:
                failed.append(src)
                print("FAILED %s\n%s" % (os.path.relpath(src, ROOT), err), flush=True)
            elif secs:
                built += 1
                if secs > 30:
                    print("  %s: %.0f s" % (os.path.relpath(src, ROOT), secs), flush=True)
    print("ps5: %d compiled, %d failed (%.0f s)" % (built, len(failed), time.time() - t0), flush=True)
    if failed:
        return 1
    with open(os.path.join(OUT, "objects.txt"), "w") as f:
        f.write("\n".join(j[1] for j in jobs) + "\n")
    if a.compile_only:
        return 0
    env = dict(os.environ, PS5_PAYLOAD_SDK=SDK, PS5_VULKAN=VULKAN, PS5_FFMPEG=FFMPEG)
    linked = subprocess.run(["bash", os.path.join(ROOT, "ps5", "tools", "link.sh"), OUT,
                             os.path.join(kit.WORK, "sa", "image", "image.bin")], env=env).returncode
    if not linked and HAS_MIHAWKGEN:   # (the network beside the game: /app0/framegen/network.fgw, vk_framegen.cpp)
        os.makedirs(os.path.join(ROOT, "dist", "PPSA99024", "framegen"), exist_ok=True)
        shutil.copy(MIHAWKGEN_NETWORK, os.path.join(ROOT, "dist", "PPSA99024", "framegen", "network.fgw"))
        rev = subprocess.run(["git", "-C", MIHAWKGEN, "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
        print("ps5: frame generation by PS5_MihawkGen (%s)" % (rev or "no revision"), flush=True)
    return linked


if __name__ == "__main__":
    sys.exit(main())
