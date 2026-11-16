# PC Installation

This repository builds executables for Linux and Windows. The Windows build uses MinGW from Linux or WSL. Running `make` without arguments builds Linux 64-bit.

## Linux

On Debian or Ubuntu, install the required tools and libraries:

```sh
sudo apt update
sudo apt install build-essential git make libpng-dev libsdl2-dev
```

Build and run the 64-bit Linux version:

```sh
make -j"$(nproc)"
./pokeemerald64
```

The equivalent explicit target is `make linux`. For 32-bit Linux, also install multilib support and SDL2 for i386:

```sh
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install gcc-multilib g++-multilib libsdl2-dev:i386
make linux IS64BIT=0 -j"$(nproc)"
./pokeemerald32
```

## Windows with WSL and MinGW

In a Debian or Ubuntu distribution under WSL, install MinGW tools for both architectures:

```sh
sudo apt update
sudo apt install build-essential git make libpng-dev \
  gcc-mingw-w64-i686 g++-mingw-w64-i686 binutils-mingw-w64-i686 \
  gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64
```

Download the MinGW development libraries from [SDL2 2.0.16](https://archive.org/download/sdl-2-2.0.16/SDL2-devel-2.0.16-mingw.tar.gz). Extract the archive at the repository root and name the extracted directory `SDL2`. It should contain `i686-w64-mingw32/` and `x86_64-w64-mingw32/`.

Build the 64-bit Windows executable:

```sh
make winwsl -j"$(nproc)"
```

For 32-bit Windows:

```sh
make winwsl IS64BIT=0 -j"$(nproc)"
```

The output executables are `pokeemerald64.exe` and `pokeemerald32.exe`. SDL2 is linked statically, so you do not need to copy `SDL2.dll` beside the executable.

## Build all supported targets

After installing the Linux 32/64-bit and Windows MinGW prerequisites above, build all four executables from the repository root with:

```sh
./build-all.sh
```

The script uses all available CPU cores by default. Set `JOBS` to choose a different parallel build count, for example `JOBS=8 ./build-all.sh`.

## Cleaning

`make tidy` removes build executables and objects, including older ROM artifacts. `make clean` also removes tools and generated resources; check `git status` before using it.

For help, see the [README](README.md) and the project Discord server.
