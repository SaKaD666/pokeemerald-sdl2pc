#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"
VULKAN_DIR="${VULKAN_DIR:-$ROOT_DIR/build/deps/Vulkan}"
VULKAN_DOWNLOAD_TMP=""
LINUX32_CHECK_TMP=""
cleanup() {
    if [[ -n "$VULKAN_DOWNLOAD_TMP" ]]; then
        rm -rf -- "$VULKAN_DOWNLOAD_TMP"
    fi
    if [[ -n "$LINUX32_CHECK_TMP" ]]; then
        rm -rf -- "$LINUX32_CHECK_TMP"
    fi
}
trap cleanup EXIT

CLEAN_BUILD=0
case "${1:-}" in
    "") ;;
    --clean) CLEAN_BUILD=1 ;;
    --help|-h)
        printf 'Usage: %s [--clean]\n' "${0##*/}"
        printf '  Default: reuse per-target object caches and rebuild changed files.\n'
        printf '  --clean: clear all four object caches and executables before building.\n'
        exit 0
        ;;
    *)
        printf 'Unknown option: %s\nUsage: %s [--clean]\n' "$1" "${0##*/}" >&2
        exit 2
        ;;
esac
if (($# > 1)); then
    printf 'Usage: %s [--clean]\n' "${0##*/}" >&2
    exit 2
fi

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
for tool in make gcc g++ cpp as objcopy pkg-config sha256sum \
    x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ x86_64-w64-mingw32-cpp \
    x86_64-w64-mingw32-as x86_64-w64-mingw32-objcopy x86_64-w64-mingw32-windres \
    i686-w64-mingw32-gcc i686-w64-mingw32-g++ i686-w64-mingw32-cpp \
    i686-w64-mingw32-as i686-w64-mingw32-objcopy i686-w64-mingw32-windres; do
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
    local headers_path="$VULKAN_DIR/include/vulkan/vulkan.h"
    local i686_library="$VULKAN_DIR/i686-w64-mingw32/lib/libvulkan-1.dll.a"
    local x86_64_library="$VULKAN_DIR/x86_64-w64-mingw32/lib/libvulkan-1.dll.a"

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

    mkdir -p "$VULKAN_DIR/include/vulkan" "$VULKAN_DIR/include/vk_video" \
        "$VULKAN_DIR/i686-w64-mingw32/lib" \
        "$VULKAN_DIR/x86_64-w64-mingw32/lib"
    cp -a "$VULKAN_DOWNLOAD_TMP/headers/mingw64/include/vulkan/." "$VULKAN_DIR/include/vulkan/"
    cp -a "$VULKAN_DOWNLOAD_TMP/headers/mingw64/include/vk_video/." "$VULKAN_DIR/include/vk_video/"
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

check_linux32_link_dependencies() {
    local -a cflags libs
    read -r -a cflags <<<"$(pkg-config --cflags sdl2 libpng vulkan)"
    read -r -a libs <<<"$(pkg-config --libs sdl2 libpng vulkan)"
    LINUX32_CHECK_TMP="$(mktemp -d)"

    if ! printf '%s\n' \
        '#include <SDL.h>' \
        '#include <png.h>' \
        '#include <vulkan/vulkan.h>' \
        'int main(void) {' \
        '    (void)SDL_Init(0);' \
        '    (void)png_access_version_number();' \
        '    (void)vkEnumerateInstanceVersion(0);' \
        '    return 0;' \
        '}' \
        | gcc -m32 "${cflags[@]}" -x c - -lSDL2main "${libs[@]}" -lm \
            -o "$LINUX32_CHECK_TMP/link-check"; then
        printf 'Linux 32-bit SDL2, libpng, or Vulkan development libraries are missing or unusable. Install the i386 dependencies described in INSTALL_PC.md.\n' >&2
        exit 1
    fi

    rm -rf -- "$LINUX32_CHECK_TMP"
    LINUX32_CHECK_TMP=""
}

check_linux32_link_dependencies

sdk="$ROOT_DIR/SDL2/x86_64-w64-mingw32"
if [[ ! -f "$sdk/include/SDL2/SDL.h" || ! -f "$sdk/lib/libSDL2.a" || ! -f "$sdk/lib/libSDL2main.a" ]]; then
    printf 'The 64-bit SDL2 MinGW SDK is missing or incomplete: %s\n' "$sdk" >&2
    printf 'Install it as described in INSTALL_PC.md.\n' >&2
    exit 1
fi

sdk="$ROOT_DIR/SDL2/i686-w64-mingw32"
if [[ ! -f "$sdk/include/SDL2/SDL.h" || ! -f "$sdk/lib/libSDL2.a" || ! -f "$sdk/lib/libSDL2main.a" ]]; then
    printf 'The 32-bit SDL2 MinGW SDK is missing or incomplete: %s\n' "$sdk" >&2
    printf 'Install it as described in INSTALL_PC.md.\n' >&2
    exit 1
fi

ensure_mingw_vulkan

for arch in i686-w64-mingw32 x86_64-w64-mingw32; do
    if [[ ! -f "$VULKAN_DIR/$arch/lib/libvulkan-1.dll.a" ]]; then
        printf 'The MinGW Vulkan import library is missing for %s.\n' "$arch" >&2
        printf 'The automatic Vulkan dependency setup did not create the expected file.\n' >&2
        exit 1
    fi
done
if [[ ! -f "$VULKAN_DIR/include/vulkan/vulkan.h" ]]; then
    printf 'The MinGW Vulkan headers are missing.\n' >&2
    printf 'The automatic Vulkan dependency setup did not create the expected headers.\n' >&2
    exit 1
fi

cache_fingerprint() {
    local target="$1"
    local width="$2"
    local target_os="$3"
    local prefix=""
    local target_arch=""
    local -a tools

    if [[ "$target" == winwsl ]]; then
        if [[ "$width" == 32 ]]; then
            target_arch=i686-w64-mingw32
        else
            target_arch=x86_64-w64-mingw32
        fi
        prefix="${target_arch}-"
        tools=("${prefix}gcc" "${prefix}cpp" "${prefix}as" "${prefix}objcopy" "${prefix}windres")
    else
        tools=(gcc cpp as objcopy)
    fi

    {
        printf 'target=%s\nwidth=%s\ntarget_os=%s\n' "$target" "$width" "$target_os"
        printf 'renderer=%s\nDINFO=%s\nO_LEVEL=%s\n' \
            "${TILE_RENDERER:-RENDERER_EASY_DRAW}" "${DINFO:-0}" "${O_LEVEL:-2}"
        printf 'CFLAGS=%s\nCPPFLAGS=%s\nASFLAGS=%s\nLDFLAGS=%s\n' \
            "${CFLAGS:-}" "${CPPFLAGS:-}" "${ASFLAGS:-}" "${LDFLAGS:-}"

        for tool in "${tools[@]}"; do
            printf '\n[%s]\n' "$tool"
            command -v "$tool"
            "$tool" --version | sed -n '1p'
        done

        if [[ "$target" == winwsl ]]; then
            local sdk="$ROOT_DIR/SDL2/$target_arch"
            sha256sum "$sdk/lib/libSDL2.a" "$sdk/lib/libSDL2main.a" \
                "$VULKAN_DIR/$target_arch/lib/libvulkan-1.dll.a"
            find "$sdk/include" "$VULKAN_DIR/include" -type f -print0 \
                | sort -z | xargs -0 sha256sum
        else
            pkg-config --modversion sdl2 libpng vulkan
            pkg-config --cflags sdl2 libpng vulkan
            pkg-config --libs sdl2 libpng vulkan
        fi

        sha256sum "$ROOT_DIR/Makefile" "$ROOT_DIR"/*.mk "$ROOT_DIR/charmap.txt"
        find "$ROOT_DIR/tools" -type f \
            \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name Makefile \) -print0 \
            | sort -z | xargs -0 sha256sum
    } | sha256sum | awk '{print $1}'
}

build_one() {
    local target="$1"
    local is64bit="$2"
    local width="$3"
    local target_os=LINUX
    local renderer="${TILE_RENDERER:-RENDERER_EASY_DRAW}"
    local dinfo="${DINFO:-0}"
    local mode=release
    local executable="$ROOT_DIR/pokeemerald$width"
    local object_dir
    local config_stamp
    local fingerprint

    if [[ "$target" == winwsl ]]; then
        target_os=WINDOWS
        executable+=".exe"
    fi

    if [[ "$dinfo" == 1 ]]; then
        mode=debug
    fi
    object_dir="$ROOT_DIR/build/pc$width/$target_os/$renderer/$mode"
    config_stamp="$object_dir/.build-config"

    printf '\n==> Building %s %s-bit\n' "$target" "$width"
    fingerprint="$(cache_fingerprint "$target" "$width" "$target_os")"
    if [[ -d "$object_dir" ]]; then
        if [[ ! -f "$config_stamp" ]] || [[ "$(<"$config_stamp")" != "$fingerprint" ]]; then
            printf 'Build configuration changed; invalidating %s-bit %s cache.\n' "$width" "$target_os"
            rm -rf -- "$object_dir"
            rm -f -- "$executable"
        fi
    fi
    mkdir -p "$object_dir"
    printf '%s\n' "$fingerprint" > "$config_stamp"

    if ! make -j"$JOBS" "IS64BIT=$is64bit" "TILE_RENDERER=$renderer" "DINFO=$dinfo" "$target"; then
        rm -f -- "$executable"
        return 1
    fi
}

if [[ "$CLEAN_BUILD" == 1 ]]; then
    printf 'Clearing all portable object caches and executables.\n'
    rm -rf -- "$ROOT_DIR/build/pc32" "$ROOT_DIR/build/pc64"
    rm -f -- "$ROOT_DIR/pokeemerald32" "$ROOT_DIR/pokeemerald32.exe" \
        "$ROOT_DIR/pokeemerald64" "$ROOT_DIR/pokeemerald64.exe"
fi

build_one linux 1 64
build_one winwsl 1 64
build_one linux 0 32
build_one winwsl 0 32

printf '\nBuilds completed:\n  pokeemerald64\n  pokeemerald64.exe\n  pokeemerald32\n  pokeemerald32.exe\n'
