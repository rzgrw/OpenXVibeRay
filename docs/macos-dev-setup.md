# macOS Development Setup Guide

This guide covers building and running viberay on macOS, based on SP1 baseline testing.

## Prerequisites

- macOS 13 or later (Apple Silicon arm64 or Intel x86_64)
- Xcode Command Line Tools (full Xcode is not required)
- Homebrew

Install Xcode Command Line Tools if you haven't already:

```bash
xcode-select --install
```

## Install Dependencies

```bash
brew install cmake ccache sdl2 lzo libogg libvorbis theora openal-soft jpeg-turbo
```

## Build

Initialize submodules first — this step is required before configuring CMake:

```bash
git submodule update --init --recursive
```

Then configure and build:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=ON \
  -DCPACK_GENERATOR=ZIP \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build -j10
```

On macOS Tahoe beta you may also need to specify the SDK path explicitly:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=ON \
  -DCPACK_GENERATOR=ZIP \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk
```

Note: avoid `--parallel $(sysctl -n hw.ncpu)` on Tahoe — `sysctl -n hw.ncpu` produces a trailing space that breaks the argument. Use an explicit `-j10` (or another number) instead.

## Binaries Location

After a successful build, binaries are placed in:

```
bin/arm64/Release/
```

Note this is not `build/bin/` — the final output lives directly under the repo root.

## Running with Call of Chernobyl

### 1. Prepare game data

Place your Call of Chernobyl game data in a directory, for example:

```
/Users/your-name/stalkercoc/
```

### 2. Copy GL shaders

The OpenGL shaders must be present under `gamedata/shaders/gl/` inside your game data directory. Copy them from the repository:

```bash
cp -r /path/to/viberay/res/gamedata/shaders/gl/ /Users/your-name/stalkercoc/gamedata/shaders/gl/
```

### 3. Launch

```bash
cd /Users/your-name/stalkercoc
DYLD_LIBRARY_PATH=/path/to/viberay/bin/arm64/Release \
  /path/to/viberay/bin/arm64/Release/xr_3da
```

Alternatively, use the `run-coc.sh` wrapper script in the repository root.

## Renderer

OpenGL (renderer_r3) is selected automatically on macOS. A Metal renderer is planned for SP2.

## Known Issues

**Shadow and lighting visual glitches**
macOS ships an older, deprecated OpenGL 4.1 implementation. Some shadow and lighting effects do not render correctly as a result. This is a platform limitation.

**DDS texture load failures**
Some DDS textures fail to load and produce OpenGL errors. This is caused by limitations in the macOS GL driver, not the engine.

**`sysctl -n hw.ncpu` trailing space on Tahoe**
On macOS Tahoe beta, `sysctl -n hw.ncpu` appends a trailing space to its output. Using `--parallel $(sysctl -n hw.ncpu)` in a build command will fail. Always pass an explicit `-j` value such as `-j10`.

**Non-fatal CoC script warnings**
Warnings about invalid object IDs and inventory slot miscounts appear in the log at startup. These are bugs in the Call of Chernobyl mod scripts, not in the engine, and do not affect gameplay.
