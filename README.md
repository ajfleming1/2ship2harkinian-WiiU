# 2 Ship 2 Harkinian - Wii U Fork

A fork of [2ship2harkinian](https://github.com/HarbourMasters/2ship2harkinian) with build fixes for Wii U cross-compilation using devkitPro.

## Wii U Release

**[Download the latest Wii U release](https://github.com/ajfleming1/2ship2harkinian-WiiU/releases)**

### Quick Setup

1. Extract the zip to your SD card
2. Place `2ship.wuhb` in `SD:/wiiu/apps/`
3. Place `2ship.o2r` in `SD:/wiiu/apps/2ship/`
4. Extract `mm.o2r` from your own Majora's Mask ROM using the Windows build (see below)
5. Place `mm.o2r` in `SD:/wiiu/apps/2ship/`
6. Launch from Homebrew Launcher on an Aroma/Tiramisu-enabled Wii U

Alternatively, place `2ship.rpx` directly in `SD:/wiiu/apps/2ship/` instead of using the `.wuhb`.

### Extracting mm.o2r

The `mm.o2r` file contains assets extracted from your own Majora's Mask ROM. You need a Windows build to generate it:

```bash
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --target ExtractAssets --config Debug
```

The resulting `mm.o2r` will be in `x64/Debug/`.

## Building for Wii U

### Prerequisites

- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitPPC toolchain
- devkitPro portlibs: `ppc-zlib ppc-bzip2 ppc-libpng ppc-tinyxml2 wiiu-sdl2 wiiu-mbedtls`
- Ninja build system

### Build Commands

From inside the devkitPro MSYS2 shell:

```bash
cmake -S . -B build-wiiu -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

ninja -C build-wiiu
```

Outputs: `build-wiiu/mm/2ship.wuhb` and `build-wiiu/mm/2ship.rpx`

See [inst.md](inst.md) for detailed build instructions and troubleshooting.

## Build Fixes in This Fork

- **libzip cross-compilation** — Added FetchContent for libzip and `LibZip::LibZip` target alias
- **CrashHandler POSIX detection** — WiiU's newlib has `unistd.h` but lacks `dlfcn.h`/`execinfo.h`
- **ar argument list too long** — Batching wrapper script for MSYS2's command line limit
- **ZAPD OTRExporter linking** — Added `--whole-archive` for CafeOS
- **main() archive ordering** — Added `--undefined=main` to force symbol resolution

## Upstream

- Upstream project: [HarbourMasters/2ship2harkinian](https://github.com/HarbourMasters/2ship2harkinian)
- Upstream Wii U fork: [HarbourMasters/2ship2harkinian-WiiU](https://github.com/HarbourMasters/2ship2harkinian-WiiU)

### Other Platforms (from upstream)

* [Windows](https://nightly.link/HarbourMasters/2ship2harkinian/workflows/main/develop/2ship-windows.zip)
* [Linux](https://nightly.link/HarbourMasters/2ship2harkinian/workflows/main/develop/2ship-linux.zip)
* [Mac](https://nightly.link/HarbourMasters/2ship2harkinian/workflows/main/develop/2ship-mac.zip)
