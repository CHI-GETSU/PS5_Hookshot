#!/usr/bin/env bash
# PS5: the whole build on a Linux PC (or Ubuntu under WSL on Windows), from your own game file to the title folder.
#
#   build-title.sh <your game.nup> [--jobs N] [--out DIR]
#   build-title.sh --packages             the packages it needs, as apt names them (Ubuntu 24.04)
#   sudo build-title.sh --install-packages  installs them (apt only)
#
# Run from a checkout, it builds that checkout. Run on its own (the file from a release), it first clones PS5_Hookshot
# at REF into a PS5_Homebrews folder where it is run. What a title links is cloned beside PS5_Hookshot, at the
# revisions this build was played with on a console, and built once:
#
#   PS5_Vulkan       the link recipe, the tool that makes the console's executable, libc.prx, its payload SDK
#   PS5_Mesa         RADV, Mesa's Vulkan driver, built for the console by PS5_Vulkan (the long step)
#   PS5_PayloadSDK   the platform layer this title is compiled with
#   PS5_MihawkGen    frame generation
#
# A project that is already there is used as it is. A step that is done is not done again: after a change, or an
# interrupted run, run it again. It never touches a console, and nothing made from your game leaves work/ and dist/.
#
# --jobs N: compile N files at once. By default your cores less two, and no more than one for each 2 GB of memory:
# ten at once took 14 GB at their peak. A build that is killed for want of memory wants fewer.
# --out DIR: also copy the finished title folder into DIR.
set -euo pipefail

REF=${HOOKSHOT_REF:-ps5-v0.1.0-beta.1}
VULKAN_REV=7f492b4c6f67a4e8c8c67a4272cfeef22abea9cc
MESA_REV=47e9eb61ee7e1112194557e1e047178562827bc6
SDK_REV=611893fc25ef3e138f5bc2c90d829d8b58174cd2
MIHAWKGEN_REV=c57232a9f16bcb6df5fe07c9bb631518f2b1cc0e
# (the headers gpu/vk is compiled with: scripts/linux/deps.sh's pins, which ps5/build.py names)
VK_HEADERS=v1.4.345 SPIRV_HEADERS=vulkan-sdk-1.4.341.0 VMA=v3.3.0
PYTHON_PACKAGES=(lz4 numpy capstone "unicorn>=2.1,<2.2" zstandard meson ninja mako pyyaml packaging)
APT_PACKAGES="git curl wget unzip xz-utils rsync make nasm pkg-config bison flex file ca-certificates python3 python3-venv \
clang lld llvm llvm-dev libclang-rt-18-dev libclang-dev libclang-cpp-dev libclc-18-dev libllvmspirvlib-18-dev spirv-tools glslang-tools \
zlib1g-dev libzstd-dev"

die() { printf '\nbuild-title: %s\n' "$*" >&2; exit 1; }
step() { printf '\n==> %s\n' "$*"; }
note() { printf '    %s\n' "$*"; }

game=${HOOKSHOT_GAME:-} out=${HOOKSHOT_OUT:-} jobs=
args=("$@")
while (( $# )); do
    case $1 in
        --packages) echo "$APT_PACKAGES"; exit 0 ;;
        --install-packages)
            command -v apt-get > /dev/null || die "--install-packages is for apt (Ubuntu, Debian): --packages lists them"
            apt-get update
            # shellcheck disable=SC2086
            DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $APT_PACKAGES
            exit 0 ;;
        --jobs) jobs=${2:?--jobs takes a number}; shift ;;
        --out) out=${2:?--out takes a folder}; shift ;;
        -h | --help) sed -n '2,23p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) die "unknown option $1 (--help says what there is)" ;;
        *) game=$1 ;;
    esac
    shift
done
[[ -n $game ]] || die "which game? build-title.sh <your game.nup>"
[[ -f $game ]] || die "no such file: $game"
game=$(realpath -- "$game")
[[ -z $out ]] || { mkdir -p -- "$out"; out=$(realpath -- "$out"); }

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ ! -f $here/build.py || ! -f $here/../nx2/lift.py ]]; then
    # On its own: the repository first, then its copy of this script takes over.
    command -v git > /dev/null || die "git is needed (sudo apt install git)"
    home=${HOOKSHOT_HOME:-$PWD/PS5_Homebrews}
    mkdir -p -- "$home"
    step "PS5_Hookshot at $REF, into $home/PS5_Hookshot"
    if [[ ! -d $home/PS5_Hookshot/.git ]]; then
        git -c advice.detachedHead=false clone -q --branch "$REF" https://github.com/mihawk-99/PS5_Hookshot.git "$home/PS5_Hookshot"
    else
        git -C "$home/PS5_Hookshot" fetch -q --tags origin
        git -C "$home/PS5_Hookshot" checkout -q "$REF" ||
            die "$home/PS5_Hookshot has changes of its own: build it with its own ps5/build-title.sh"
    fi
    HOOKSHOT_GAME=$game HOOKSHOT_OUT=$out exec bash "$home/PS5_Hookshot/ps5/build-title.sh" "${args[@]}"
fi
root=$(dirname "$here")
parent=$(dirname "$root")
logs="$root/work/build-title"
deps="$root/.local/ps5-deps"
mkdir -p "$logs" "$deps/downloads"
cd "$root"

run() {   # log-name command...: quiet while it works, the end of its log when it fails
    local log="$logs/$1.log" start=$SECONDS
    shift
    "$@" > "$log" 2>&1 || { tail -n 30 "$log" >&2; die "that step failed; the whole log: $log"; }
    note "done in $(( SECONDS - start )) s"
}

step "this PC"
missing=()
for command in git curl wget unzip tar xz rsync make nasm pkg-config bison flex python3 clang clang++ llvm-ar \
        glslangValidator flock sha256sum; do
    command -v "$command" > /dev/null || missing+=("$command")
done
if (( ${#missing[@]} )); then
    note "not found: ${missing[*]}"
    if command -v apt-get > /dev/null; then
        note "install what this needs with:"
        note "  sudo bash ${BASH_SOURCE[0]} --install-packages"
    else
        note "install them with your system's packages (and LLVM's and Clang's development files, libclc,"
        note "the SPIR-V LLVM translator and SPIRV-Tools, which the driver's build needs)"
    fi
    exit 1
fi
clang_major=$(clang --version | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1)
[[ -n $clang_major && $clang_major -ge 18 ]] || die "clang 18 or later is needed (this one: ${clang_major:-unknown})"
note "ok: clang $clang_major, $(nproc) cores, $(awk '/MemTotal/ { printf "%.0f GB of memory", $2 / 1048576 }' /proc/meminfo)"

step "Python's packages, in .local/venv"
python="$root/.local/venv/bin/python"
stamp="${PYTHON_PACKAGES[*]}"
if [[ ! -x $python || $(cat "$root/.local/venv/.packages" 2> /dev/null) != "$stamp" ]]; then
    [[ -x $python ]] || python3 -m venv "$root/.local/venv" ||
        die "python3 cannot make a virtual environment (sudo apt install python3-venv)"
    run pip "$python" -m pip install --disable-pip-version-check "${PYTHON_PACKAGES[@]}"
    printf '%s\n' "$stamp" > "$root/.local/venv/.packages"
else
    note "there already"
fi
export PATH="$root/.local/venv/bin:$PATH"

step "the projects beside this one"
project() {   # name revision [clone options]
    local name=$1 revision=$2 path="$parent/$1"
    shift 2
    if [[ -d $path/.git ]]; then
        note "$name: there already, used as it is ($(git -C "$path" rev-parse --short HEAD))"
    else
        note "$name: cloning"
        git clone -q "$@" "https://github.com/mihawk-99/$name.git" "$path"
        [[ " $* " == *" --no-checkout "* ]] || git -C "$path" checkout -q --detach "$revision"
    fi
    git -C "$path" cat-file -e "$revision^{commit}" 2> /dev/null || die "$path does not have $revision: update it (git fetch)"
}
vulkan="$parent/PS5_Vulkan"
project PS5_Vulkan "$VULKAN_REV"
project PS5_Mesa "$MESA_REV" --no-checkout   # (its revision is exported by PS5_Vulkan's build: no files of its own)
project PS5_PayloadSDK "$SDK_REV" --no-checkout
project PS5_MihawkGen "$MIHAWKGEN_REV"

step "PS5_Vulkan: its payload SDK"
sdk_pin=$(sed -n 's/^sdk_revision=//p' "$vulkan/tools/setup-native-dependencies.sh")
if [[ $(cat "$vulkan/.deps/native/ps5-payload-sdk/.ps5-sdk-revision" 2> /dev/null) == "$sdk_pin" ]]; then
    note "there already"
else
    (cd "$vulkan" && run vulkan-sdk bash tools/setup-native-dependencies.sh)
fi

step "PS5_Vulkan: the tool that makes the console's executable, and libc.prx"
if [[ -x $vulkan/build/host/ps5-native-tool && -f $vulkan/runtime/libc.prx ]]; then
    note "there already"
else
    # (make goes on to PS5_Vulkan's own sample title, which this build does not need: the two files are what counts)
    (cd "$vulkan" && make > "$logs/vulkan-make.log" 2>&1) || true
    [[ -x $vulkan/build/host/ps5-native-tool && -f $vulkan/runtime/libc.prx ]] ||
        { tail -n 30 "$logs/vulkan-make.log" >&2; die "PS5_Vulkan's make did not build them; the whole log: $logs/vulkan-make.log"; }
    note "done"
fi

step "RADV, the Vulkan driver, at PS5 Mesa ${MESA_REV:0:11} (the long step: 10 to 30 minutes the first time)"
archive="$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
(cd "$vulkan" && PS5_MESA_FORK="$parent/PS5_Mesa" PS5_MESA_REVISION=$MESA_REV run radv bash tools/build-radv.sh release)
[[ -f $archive ]] || die "no RADV archive at $archive"

step "the payload SDK this title is compiled with, at ${SDK_REV:0:11}"
sdk="$deps/ps5-payload-sdk"
if [[ $(cat "$sdk/.ps5-sdk-revision" 2> /dev/null) == "$SDK_REV" ]]; then
    note "there already"
else
    tree=$(mktemp -d)
    git -C "$parent/PS5_PayloadSDK" archive "$SDK_REV" | tar -x -C "$tree"
    run sdk bash "$tree/platform/tools/setup-sdk.sh" "$sdk" "$SDK_REV" "$deps/downloads"
    rm -rf -- "$tree"
fi
[[ -x $sdk/bin/prospero-lld ]] || die "the payload SDK at $sdk is incomplete"
export PS5_PAYLOAD_SDK="$sdk" PS5_VULKAN="$vulkan" PS5_MIHAWKGEN="$parent/PS5_MihawkGen"

step "the Vulkan, SPIR-V and VMA headers"
headers() {   # repository tag name
    local to="$deps/cpm/$3/$2"
    [[ -d $to/include ]] && return
    curl -fsSL --retry 5 --retry-all-errors --proto '=https' -o "$deps/downloads/$3-$2.tar.gz" "https://github.com/$1/archive/refs/tags/$2.tar.gz"
    mkdir -p "$to"
    tar -xzf "$deps/downloads/$3-$2.tar.gz" -C "$to" --strip-components=1
}
headers KhronosGroup/Vulkan-Headers "$VK_HEADERS" vulkanheaders
headers KhronosGroup/SPIRV-Headers "$SPIRV_HEADERS" spirv-headers
headers GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator "$VMA" vulkanmemoryallocator
note "there"

step "your game: its program read out of $(basename -- "$game") and translated to C (2 to 5 minutes)"
game_stamp="$(stat -c '%s %Y' -- "$game") $(git -C "$root" rev-parse HEAD 2> /dev/null || echo none)"
if [[ -f work/lift/sa/gen/a64_table.c && -f work/lift/sa/image/image.bin && $(cat work/lift/.game 2> /dev/null) == "$game_stamp" ]]; then
    note "done already, from this file"
else
    run game-extract "$python" -I -u -X utf8 nx2/nup.py "$game" --only exefs --extract "$root/work/game"
    mkdir -p "$root/work/lift/image"   # (the lift writes its chunk list there and does not make the folder)
    OOT_WORK="$root/work/lift" run game-lift "$python" -I -u -X utf8 nx2/lift.py --standalone --write \
        --exefs "$root/work/game/exefs" --work "$root/work/lift"
    printf '%s\n' "$game_stamp" > work/lift/.game
fi

step "FFmpeg and zstd for the console (the game's movies, and the game file's compressed parts)"
run ffmpeg bash ps5/tools/build-ffmpeg.sh
run zstd bash ps5/tools/build-zstd.sh

step "the title: the game, the runtime and the renderer compiled and linked (20 to 60 minutes the first time)"
title="$root/dist/PPSA99024"
if [[ -z $jobs ]]; then
    jobs=$(awk -v cores="$(nproc)" '/MemTotal/ { m = int($2 / 2097152); c = cores - 2; j = c < m ? c : m; print j < 1 ? 1 : j }' /proc/meminfo)
fi
note "$jobs files at once (--jobs)"
"$python" -I ps5/build.py --jobs "$jobs" 2>&1 | tee "$logs/title.log" ||
    die "the title was not built; the whole log: $logs/title.log (if the compiler was killed for want of memory, use --jobs 2)"
[[ -f $title/eboot.bin && -f $title/image.bin ]] || die "no title at $title; the log: $logs/title.log"
if [[ -n $out ]]; then
    rm -rf -- "$out/PPSA99024"
    cp -r -- "$title" "$out/PPSA99024"
    title="$out/PPSA99024"
fi

cat << EOF

Built: $title ($(du -sh -- "$title" | cut -f1))

On the console, with its homebrew environment running and an FTP server on (port 2121 with ftpsrv):
  1. Copy the whole PPSA99024 folder to /data/homebrew/PPSA99024.
  2. Copy your game file to /data/lichii/roms/oot/oot.nup (make the folders; the name must be oot.nup).
  3. Start Hookshot from the home screen (ShadowMountPlus puts it there), or from your homebrew launcher.
image.bin and eboot.bin in that folder are made from your game: keep them on your PC and your console.
EOF
