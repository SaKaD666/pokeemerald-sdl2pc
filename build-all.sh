#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"
VULKAN_DOWNLOAD_TMP=""
trap 'if [[ -n "$VULKAN_DOWNLOAD_TMP" ]]; then rm -rf -- "$VULKAN_DOWNLOAD_TMP"; fi' EXIT

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

ensure_mingw_vulkan() {
    local headers_path="$ROOT_DIR/Vulkan/include/vulkan/vulkan.h"
    local i686_library="$ROOT_DIR/Vulkan/i686-w64-mingw32/lib/libvulkan-1.dll.a"
    local x86_64_library="$ROOT_DIR/Vulkan/x86_64-w64-mingw32/lib/libvulkan-1.dll.a"

    if [[ -f "$headers_path" && -f "$i686_library" && -f "$x86_64_library" ]]; then
        return
    fi

    for tool in curl sha256sum zstd tar; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            printf 'Missing Vulkan dependency setup tool: %s\n' "$tool" >&2
            exit 1
        fi
    done

    local version='1~1.4.357.0-1'
    VULKAN_DOWNLOAD_TMP="$(mktemp -d)"

    download_package() {
        local repo="$1"
        local package="$2"
        local expected_sha256="$3"
        local archive="$VULKAN_DOWNLOAD_TMP/$package.pkg.tar.zst"
        local url="https://mirror.msys2.org/mingw/$repo/$package-$version-any.pkg.tar.zst"

        curl --fail --location --silent --show-error "$url" --output "$archive" || {
            printf 'Could not download %s from %s\n' "$package" "$url" >&2
            return 1
        }
        if ! printf '%s  %s\n' "$expected_sha256" "$archive" | sha256sum --check --status; then
            printf 'Checksum verification failed for %s\n' "$package" >&2
            return 1
        fi
        printf '%s\n' "$archive"
    }

    printf 'Preparing MinGW Vulkan headers and import libraries...\n'
    local headers_archive i686_archive x86_64_archive
    headers_archive="$(download_package mingw64 mingw-w64-x86_64-vulkan-headers 68c7b758194a255c66d08fe484fe615e254fad9259a70c9dc9d9bec9e51b1384)"
    i686_archive="$(download_package mingw32 mingw-w64-i686-vulkan-loader 2799de5c1e316dc76925fba6a2b5c99f508ede1273a5ecd73625c2b29fe2232c)"
    x86_64_archive="$(download_package mingw64 mingw-w64-x86_64-vulkan-loader 4050dcd32f011f28b4784bf8bccdd48681c3bc5f0efee02f897566abb96bbb76)"

    mkdir -p "$VULKAN_DOWNLOAD_TMP/headers" "$VULKAN_DOWNLOAD_TMP/i686" "$VULKAN_DOWNLOAD_TMP/x86_64"
    tar --zstd -xf "$headers_archive" -C "$VULKAN_DOWNLOAD_TMP/headers" mingw64/include/vulkan mingw64/include/vk_video
    tar --zstd -xf "$i686_archive" -C "$VULKAN_DOWNLOAD_TMP/i686" mingw32/lib/libvulkan-1.dll.a
    tar --zstd -xf "$x86_64_archive" -C "$VULKAN_DOWNLOAD_TMP/x86_64" mingw64/lib/libvulkan-1.dll.a

    mkdir -p "$ROOT_DIR/Vulkan/include/vulkan" "$ROOT_DIR/Vulkan/include/vk_video" \
        "$ROOT_DIR/Vulkan/i686-w64-mingw32/lib" \
        "$ROOT_DIR/Vulkan/x86_64-w64-mingw32/lib"
    cp -a "$VULKAN_DOWNLOAD_TMP/headers/mingw64/include/vulkan/." "$ROOT_DIR/Vulkan/include/vulkan/"
    cp -a "$VULKAN_DOWNLOAD_TMP/headers/mingw64/include/vk_video/." "$ROOT_DIR/Vulkan/include/vk_video/"
    install -m 0644 "$VULKAN_DOWNLOAD_TMP/i686/mingw32/lib/libvulkan-1.dll.a" "$i686_library"
    install -m 0644 "$VULKAN_DOWNLOAD_TMP/x86_64/mingw64/lib/libvulkan-1.dll.a" "$x86_64_library"
    rm -rf -- "$VULKAN_DOWNLOAD_TMP"
    VULKAN_DOWNLOAD_TMP=""
    printf 'MinGW Vulkan dependencies ready.\n'
}

if ! pkg-config --exists sdl2 libpng vulkan; then
    printf 'Linux SDL2, libpng, and Vulkan development packages are required; see INSTALL_PC.md.\n' >&2
    exit 1
fi

for file in "$(gcc -m32 -print-file-name=Scrt1.o)" \
    "$(gcc -m32 -print-file-name=libSDL2.so)" \
    "$(gcc -m32 -print-file-name=libvulkan.so)"; do
    if [[ ! -f "$file" ]]; then
        printf 'Linux 32-bit runtime/SDL2/Vulkan dependencies are missing (%s); see INSTALL_PC.md.\n' "$file" >&2
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

ensure_mingw_vulkan

for arch in i686-w64-mingw32 x86_64-w64-mingw32; do
    if [[ ! -f "$ROOT_DIR/Vulkan/$arch/lib/libvulkan-1.dll.a" ]]; then
        printf 'The MinGW Vulkan import library is missing for %s.\n' "$arch" >&2
        printf 'The automatic Vulkan dependency setup did not create the expected file.\n' >&2
        exit 1
    fi
done
if [[ ! -f "$ROOT_DIR/Vulkan/include/vulkan/vulkan.h" ]]; then
    printf 'The MinGW Vulkan headers are missing.\n' >&2
    printf 'The automatic Vulkan dependency setup did not create the expected headers.\n' >&2
    exit 1
fi

build_one() {
    local target="$1"
    local is64bit="$2"
    local width="$3"
    local object_dir="$ROOT_DIR/build/pc$width"
    local executable="$ROOT_DIR/pokeemerald$width"

    if [[ "$target" == winwsl ]]; then
        executable+=".exe"
    fi

    printf '\n==> Building %s %s-bit\n' "$target" "$width"
    # Linux and Windows share this object path per architecture. Make does not
    # track compiler/flag changes; remove both the cache and output so every
    # requested target is actually rebuilt.
    rm -rf -- "$object_dir"
    rm -f -- "$executable"
    make -j"$JOBS" "IS64BIT=$is64bit" "$target"
}

build_one linux 1 64
build_one linux 0 32
build_one winwsl 1 64
build_one winwsl 0 32

printf '\nAll builds completed:\n  pokeemerald64\n  pokeemerald32\n  pokeemerald64.exe\n  pokeemerald32.exe\n'
