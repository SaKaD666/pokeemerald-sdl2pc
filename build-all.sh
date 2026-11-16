#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

JOBS="${JOBS:-}"
if [[ -z "$JOBS" ]]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    else
        JOBS=1
    fi
fi

if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    printf 'JOBS must be a positive integer (got: %s)\n' "$JOBS" >&2
    exit 2
fi

missing=()
for tool in make gcc g++ cpp as objcopy pkg-config \
    i686-w64-mingw32-gcc i686-w64-mingw32-g++ i686-w64-mingw32-cpp \
    i686-w64-mingw32-as i686-w64-mingw32-objcopy i686-w64-mingw32-windres \
    x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ x86_64-w64-mingw32-cpp \
    x86_64-w64-mingw32-as x86_64-w64-mingw32-objcopy x86_64-w64-mingw32-windres; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        missing+=("$tool")
    fi
done

if ((${#missing[@]} > 0)); then
    printf 'Missing build tools:\n' >&2
    printf '  %s\n' "${missing[@]}" >&2
    printf 'Install the prerequisites described in INSTALL_PC.md.\n' >&2
    exit 1
fi

if ! pkg-config --exists sdl2 libpng; then
    printf 'Linux SDL2 and libpng development packages are required; see INSTALL_PC.md.\n' >&2
    exit 1
fi

for file in "$(gcc -m32 -print-file-name=Scrt1.o)" "$(gcc -m32 -print-file-name=libSDL2.so)"; do
    if [[ ! -f "$file" ]]; then
        printf 'Linux 32-bit runtime/SDL2 dependencies are missing (%s); see INSTALL_PC.md.\n' "$file" >&2
        exit 1
    fi
done

for arch in i686-w64-mingw32 x86_64-w64-mingw32; do
    sdk="$ROOT_DIR/SDL2/$arch"
    if [[ ! -f "$sdk/include/SDL2/SDL.h" || ! -f "$sdk/lib/libSDL2.a" || ! -f "$sdk/lib/libSDL2main.a" ]]; then
        printf 'The SDL2 MinGW SDK is missing or incomplete: %s\n' "$sdk" >&2
        printf 'Install it as described in INSTALL_PC.md.\n' >&2
        exit 1
    fi
done

build_one() {
    local target="$1"
    local is64bit="$2"
    local width="$3"
    local object_dir="$ROOT_DIR/build/pc$width"

    printf '\n==> Building %s %s-bit\n' "$target" "$width"
    # Linux and Windows currently share this object path per architecture.
    # Make does not track compiler/flag changes, so discard that cache first.
    rm -rf -- "$object_dir"
    make -j"$JOBS" "IS64BIT=$is64bit" TARGET_PLATFORM=PLATFORM_SDL2 "$target"
}

build_one linux 1 64
build_one linux 0 32
build_one winwsl 1 64
build_one winwsl 0 32

printf '\nAll builds completed:\n  pokeemerald64\n  pokeemerald32\n  pokeemerald64.exe\n  pokeemerald32.exe\n'
