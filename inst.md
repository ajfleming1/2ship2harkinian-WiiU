# 2Ship2Harkinian - Build Instructions

## Table of Contents
- [Windows (Visual Studio) Build](#windows-visual-studio-build)
- [Wii U (devkitPro) Cross-Compilation](#wii-u-devkitpro-cross-compilation)
- [Deploying to Wii U](#deploying-to-wii-u)
- [Build Fixes Reference](#build-fixes-reference)

---

## Windows (Visual Studio) Build

### Prerequisites
- Visual Studio 2022 with C++ desktop development workload
- Python 3 (for asset extraction)
- A legally obtained Majora's Mask ROM

### 1. Generate Visual Studio Solution

```
cmake -S . -B "build-vs" -G "Visual Studio 17 2022" -A x64
```

- `-S .` — source directory (repo root)
- `-B "build-vs"` — build output directory
- `-G "Visual Studio 17 2022"` — use VS2022 generator
- `-A x64` — target 64-bit architecture

### 2. Build the Project

Open `build-vs/2s2h.sln` in Visual Studio, or build from command line:

```
cmake --build build-vs --config Debug
```

Output lands in `x64/Debug/` including `2ship.exe`.

### 3. Extract Assets

You need a Majora's Mask ROM placed where the extractor can find it.
Build and run the `ExtractAssets` target:

```
cmake --build build-vs --target ExtractAssets --config Debug
```

This runs ZAPD to extract ROM assets and produces:
- `mm.o2r` — ROM-extracted game assets
- `2ship.o2r` — custom port assets

These files end up in `x64/Debug/` (or wherever your build output is).

### 4. Run

Launch `x64/Debug/2ship.exe`. The `.o2r` files must be in the same directory.

---

## Wii U (devkitPro) Cross-Compilation

### Prerequisites

1. **devkitPro** with devkitPPC toolchain installed
   - Download from https://devkitpro.org/wiki/Getting_Started
   - Installed to `C:\devkitPro` by default on Windows
   - Includes its own MSYS2 environment at `C:\devkitPro\msys2\`

2. **devkitPro portlibs** — install these packages inside devkitPro's MSYS2:
   ```
   # Open devkitPro MSYS2 shell, then:
   (dkp-)pacman -S ppc-zlib ppc-bzip2 ppc-libpng ppc-tinyxml2 wiiu-sdl2 wiiu-mbedtls
   ```

3. **Previously built Windows version** — you need the extracted `.o2r` asset
   files from a Windows build (the WiiU-cross-compiled ZAPD can't run on your PC
   to extract assets).

### 1. Open devkitPro MSYS2 Shell

From PowerShell or cmd, you can invoke devkitPro's bash directly:

```powershell
$env:MSYSTEM='MSYS'
C:\devkitPro\msys2\usr\bin\bash.exe --login
```

Or from an existing MSYS2/Git Bash shell:

```bash
MSYSTEM=MSYS /c/devkitPro/msys2/usr/bin/bash.exe --login
```

All subsequent commands should be run inside this shell.

### 2. Navigate to Source

```bash
cd /c/Users/<you>/Desktop/two-ship/2ship2harkinian
```

### 3. Configure with CMake

```bash
cmake -S . -B build-wiiu -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

- `-G Ninja` — use Ninja build system (faster than Make)
- `-DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake` — devkitPro's WiiU
  cross-compilation toolchain (sets compiler, sysroot, etc.)
- `-DCMAKE_BUILD_TYPE=RelWithDebInfo` — optimized build with debug symbols

### 4. Build

```bash
ninja -C build-wiiu 2>&1 | tee build-output.log
```

- `ninja -C build-wiiu` — build in the build-wiiu directory
- `2>&1 | tee build-output.log` — show output AND save to log file for review

This produces ~1581 compilation steps. The final outputs are:
- `build-wiiu/mm/2ship.rpx` — WiiU executable
- `build-wiiu/mm/2ship.wuhb` — WiiU Homebrew Bundle (includes the .rpx + metadata)

### 5. Clean Rebuild (if needed)

```bash
rm -rf build-wiiu && cmake -S . -B build-wiiu -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo && \
  ninja -C build-wiiu 2>&1 | tee build-output.log
```

### One-Liner from PowerShell

If you want to kick off the full build from PowerShell without manually
opening the devkitPro shell:

```powershell
$env:MSYSTEM='MSYS'; C:\devkitPro\msys2\usr\bin\bash.exe --login -c "cd 'C:/Users/<you>/Desktop/two-ship/2ship2harkinian' && rm -rf build-wiiu && cmake -S . -Bbuild-wiiu -GNinja -DCMAKE_TOOLCHAIN_FILE=`$DEVKITPRO/cmake/WiiU.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo && ninja -C build-wiiu 2>&1 | tee build-output.log"
```

---

## Deploying to Wii U

### Prerequisites
- Homebrew-enabled Wii U (Aroma or Tiramisu environment)
- SD card accessible from your PC

### SD Card Layout

```
SD:/wiiu/apps/2ship.wuhb              <-- Homebrew bundle
SD:/wiiu/apps/2ship/mm.o2r            <-- ROM-extracted assets (from Windows build)
SD:/wiiu/apps/2ship/2ship.o2r         <-- Custom assets (from Windows build)
```

The app name `2ship` comes from `BenPort.h` — on startup the game does
`chdir("/vol/external01/wiiu/apps/2ship/")` and looks for `.o2r` files there.

### Source Files on PC

| File | Source |
|------|--------|
| `2ship.wuhb` | `build-wiiu/mm/2ship.wuhb` |
| `mm.o2r` | `x64/Debug/mm.o2r` (from Windows build) |
| `2ship.o2r` | `x64/Debug/2ship.o2r` (from Windows build) |

### Launch

1. Insert SD card into Wii U
2. Boot into Aroma/Tiramisu environment
3. Open Homebrew Launcher
4. Launch "Two Ship Two Harkinian"

---

## Build Fixes Reference

These are the issues encountered during WiiU cross-compilation and how they
were resolved. Documented here in case they come up again or for upstream PRs.

### 1. LibZip::LibZip target not found

**Error:** CMake configure fails — OTRExporter links `LibZip::LibZip` but no
target exists for WiiU.

**Root cause:** `libultraship/cmake/dependencies/wiiu.cmake` didn't provide
libzip. The host MSYS2 libzip gets found by `find_package` but provides
`libzip::zip` (an IMPORTED target), not the `zip` build target needed for
aliasing.

**Fix** (`libultraship/cmake/dependencies/wiiu.cmake`):
- Always FetchContent libzip from source for WiiU (no `find_package` guard)
- Create alias: `add_library(LibZip::LibZip ALIAS zip)`

### 2. dlfcn.h: No such file or directory

**Error:** `ZAPDTR/ZAPD/CrashHandler.cpp` fails to compile — includes
`<dlfcn.h>` which doesn't exist on WiiU's newlib.

**Root cause:** The `#if __has_include(<unistd.h>)` check passes on WiiU
(newlib has unistd.h) but the POSIX code path also needs `<dlfcn.h>` and
`<execinfo.h>` which WiiU lacks.

**Fix** (`ZAPDTR/ZAPD/CrashHandler.cpp`):
```c
// Was:
#if __has_include(<unistd.h>)
// Changed to:
#if __has_include(<unistd.h>) && __has_include(<dlfcn.h>) && __has_include(<execinfo.h>)
```

### 3. Argument list too long (ar)

**Error:** `powerpc-eabi-gcc-ar.exe` fails creating `lib2ship_lib.a` —
hundreds of object files exceed MSYS2's command line length limit (~32KB).

**Root cause:** MSYS2 translates paths on command lines but NOT inside
response files (.rsp). Since devkitPro's native Windows GCC can't resolve
MSYS2 virtual paths from file contents, response files don't work either.

**Fix:**
- Created `CMake/ar-wrapper.sh` — a shell script that batches object files
  in groups of 200 and calls `ar` multiple times
- Added CMake rules in `CMakeLists.txt` to use the wrapper for CafeOS:
  ```cmake
  set(CMAKE_C_ARCHIVE_CREATE "${CMAKE_SOURCE_DIR}/CMake/ar-wrapper.sh ...")
  ```

### 4. undefined reference to ImportExporters

**Error:** Linking `ZAPD.elf` fails — `ImportExporters()` is defined in
OTRExporter but the CafeOS section of ZAPD's CMakeLists didn't link it.

**Root cause:** The CafeOS `ADDITIONAL_LIBRARY_DEPENDENCIES` was missing
OTRExporter entirely. Other platforms (Linux, Switch) include it with
`--whole-archive`.

**Fix** (`ZAPDTR/ZAPD/CMakeLists.txt`):
```cmake
elseif(CMAKE_SYSTEM_NAME STREQUAL "CafeOS")
    set(ADDITIONAL_LIBRARY_DEPENDENCIES
        -Wl,--whole-archive $<TARGET_LINKER_FILE_DIR:OTRExporter>/...
        -Wl,--no-whole-archive
        "libultraship;"
        PNG::PNG
    )
```

### 5. undefined reference to main

**Error:** Linking `2ship.elf` fails — WUT's CRT (`crt0_rpx.o`) references
`main` but the linker already passed `lib2ship_lib.a` by the time it needs it.

**Root cause:** `main()` is defined in `mm/src/code/main.c` inside the static
archive `lib2ship_lib.a`. The linker processes archives lazily — it only pulls
objects that resolve currently-undefined symbols. WUT's CRT is added by the
specs file after the archives, so `main` isn't needed when the archive is
scanned.

**Fix** (`mm/CMakeLists.txt`):
```cmake
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--undefined=main)
```

This tells the linker to treat `main` as undefined from the start, forcing
it to be pulled from the archive.

### Key Lesson: MSYS2 Path Translation

MSYS2 automatically translates paths on command lines (e.g.,
`/opt/devkitpro/...` becomes `C:/devkitPro/...`). However, this translation
does NOT apply to file contents, including response files (.rsp). Native
Windows executables (like devkitPro's GCC) cannot resolve MSYS2 virtual paths
from inside files. This is why `CMAKE_NINJA_FORCE_RESPONSE_FILE` breaks
compilation but the ar-wrapper approach (which uses command-line arguments)
works.
