# Local development

This guide covers dependency installation, configuration, compilation, testing,
and running Frame Extractor from source on every maintained development
platform. Run all project commands from the repository root.

If you only want to run Frame Extractor, download a packaged build as described
in the [README](../README.md#download). Packaged builds
do not require Homebrew, vcpkg, a compiler, or the source tree. The tools below
are required only when building the project yourself.

With the GUI enabled, the first CMake configure requires network access to
fetch Dear ImGui 1.92.8. Subsequent builds reuse the downloaded source and
compiled dependencies.

## Supported development environments

| Platform | Toolchain | Dependency source | Development output |
| --- | --- | --- | --- |
| macOS 15 or newer | Apple Clang / Xcode tools | Homebrew | `build/dev` |
| Windows 10/11 x64 | Visual Studio 2022 or newer | vcpkg | `build/dev` |
| Ubuntu 24.04 x64 | GCC or Clang | vcpkg | `build/dev` |

All platforms require [Git](https://git-scm.com/downloads),
[CMake 3.25 or newer](https://cmake.org/download/), and a C++20 compiler. The
GUI also requires a working desktop session. Add
`-DFRAME_EXTRACTOR_BUILD_GUI=OFF` to a configure command for a CLI-only build.

## Codebase overview

```text
include/frame_extractor/  Public processing interfaces
src/core/                Video decoding, tracking, selection, and output
src/gui/                 SDL3/ImGui desktop application
src/cli/                 Command-line application
tests/                   Unit and integration tests
fixtures/                Synthetic regression videos
configs/profiles/        Low, Medium, and High extraction profiles
```

The GUI and CLI share the same processing core. Keep interface and platform
code outside `src/core`, and cover observable behaviour with tests.

## macOS

### Install the tools and libraries

Install Apple's command-line developer tools if they are not already present:

```sh
xcode-select --install
```

If `brew --version` reports that the command does not exist, follow the
[official Homebrew installation instructions](https://docs.brew.sh/Installation).
Run the shell setup command printed by the Homebrew installer, then confirm that
the installation is available:

```sh
brew --version
```

Install the project dependencies with Homebrew:

```sh
brew install cmake catch2 ffmpeg opencv@4 sdl3 yaml-cpp
```

### Configure and compile

```sh
cmake --preset dev \
  -DOpenCV_DIR="$(brew --prefix opencv@4)/lib/cmake/opencv4"
cmake --build --preset dev --parallel
```

The explicit OpenCV path is needed because Homebrew's versioned `opencv@4`
formula is not always found automatically.

### Run the tests

```sh
ctest --preset dev --output-on-failure
```

### Run the application

```sh
open "build/dev/Frame Extractor.app"
```

To open a video and preselect an output directory at launch:

```sh
open "build/dev/Frame Extractor.app" --args /path/to/video.mp4 /path/to/output
```

Run the command-line application directly with:

```sh
./build/dev/frame-extractor --help
```

This Homebrew-based build is intended for development on the same Mac. It
resolves its third-party libraries through Homebrew and is not a portable app
bundle. Use a CI artifact or the standalone packaging workflow described in
the [release guide](releasing.md) when
testing distribution.

## Windows

Run these commands in PowerShell.

### Install the tools

Install:

- [Visual Studio 2022 or newer](https://visualstudio.microsoft.com/downloads/)
  with the **Desktop development with C++** workload;
- [Git for Windows](https://git-scm.com/download/win);
- [CMake 3.25 or newer](https://cmake.org/download/) if it is not included in
  the selected Visual Studio components.

vcpkg is not included in this repository. The following commands clone and
bootstrap it in a stable location, following Microsoft's
[vcpkg getting-started guide](https://learn.microsoft.com/vcpkg/get_started/get-started):

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\dev\vcpkg"
```

The environment variable only applies to the current PowerShell session unless
you add it to your user environment.

### Install the libraries

From the Frame Extractor repository root:

```powershell
& "$env:VCPKG_ROOT\vcpkg.exe" install --triplet x64-windows
```

The checked-in `vcpkg.json` selects Catch2, FFmpeg, OpenCV, SDL3, and yaml-cpp.
The standard `x64-windows` triplet provides both Debug and Release libraries for
local development.

### Configure and compile

```powershell
cmake -S . -B build/dev -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DFRAME_EXTRACTOR_BUILD_TESTS=ON

cmake --build build/dev --config Debug --parallel
```

### Run the tests

```powershell
ctest --test-dir build/dev -C Debug --output-on-failure
```

### Run the application

```powershell
& ".\build\dev\Debug\frame-extractor-gui.exe"
```

Optional input and output arguments can be supplied directly:

```powershell
& ".\build\dev\Debug\frame-extractor-gui.exe" `
  "C:\path\to\video.mp4" "C:\path\to\output"
```

Run the command-line application with:

```powershell
& ".\build\dev\Debug\frame-extractor.exe" --help
```

## Linux

These instructions target the CI-tested Ubuntu 24.04 x64 environment. Linux
support remains experimental; other distributions require equivalent compiler,
desktop, and vcpkg build packages.

### Install the tools and system development packages

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git curl zip unzip \
  tar pkg-config autoconf automake libtool libltdl-dev bison gperf nasm \
  python3-jinja2 libx11-dev libxft-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev libegl1-mesa-dev libibus-1.0-dev \
  xvfb xauth patchelf
```

vcpkg is not included in this repository. After installing the system packages
above, the following commands clone and bootstrap it in a stable location,
following Microsoft's
[vcpkg getting-started guide](https://learn.microsoft.com/vcpkg/get_started/get-started):

```sh
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
export VCPKG_ROOT="$HOME/vcpkg"
```

Add `VCPKG_ROOT` to your shell profile if you want it available in future
terminal sessions.

### Install the libraries

From the Frame Extractor repository root:

```sh
"$VCPKG_ROOT/vcpkg" install --triplet x64-linux
```

The checked-in `vcpkg.json` selects Catch2, FFmpeg, OpenCV, SDL3, and yaml-cpp.
The standard `x64-linux` triplet provides Debug libraries suitable for local
development.

### Configure and compile

```sh
cmake --preset dev \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux
cmake --build --preset dev --parallel
```

### Run the tests

Run tests in a desktop session with:

```sh
ctest --preset dev --output-on-failure
```

On a headless machine, use Xvfb and software rendering:

```sh
SDL_VIDEO_DRIVER=x11 SDL_RENDER_DRIVER=software \
  xvfb-run -a ctest --test-dir build/dev --output-on-failure
```

### Run the application

```sh
./build/dev/frame-extractor-gui
```

Optional input and output arguments can be supplied directly:

```sh
./build/dev/frame-extractor-gui /path/to/video.mp4 /path/to/output
```

Run the command-line application with:

```sh
./build/dev/frame-extractor --help
```

## Rebuilding and changing configuration

For an ordinary rebuild, rerun only the build command for your platform. CMake
recompiles files that changed.

If `build` or `build/dev` was deleted, rerun the configure command first. CMake
will recreate the directory, after which the normal build and test commands
work again.

When changing the compiler, generator, architecture, or vcpkg triplet, remove
only the corresponding build directory and configure it again. Do not reuse a
CMake cache created for a different toolchain.

## Common configuration options

Pass these options on the initial CMake configure command when needed:

| Option | Effect |
| --- | --- |
| `-DFRAME_EXTRACTOR_BUILD_GUI=OFF` | Build only the core library and CLI |
| `-DFRAME_EXTRACTOR_BUILD_TESTS=OFF` | Skip test targets |
| `-DFRAME_EXTRACTOR_GUI_SMOKE_TEST=ON` | Enable the GUI startup/rendering test |

The presets enable tests and the GUI by default.

## Troubleshooting

### CMake cannot find OpenCV on macOS

Reconfigure with the documented `OpenCV_DIR` argument. Confirm that
`brew --prefix opencv@4` succeeds before running CMake.

### A macOS linker warning mentions a newer deployment target

Homebrew libraries may have been built using the host macOS SDK and can emit a
warning while the application itself targets macOS 14. This concerns the local
Homebrew development build. The standalone macOS package uses the controlled
vcpkg triplet and verifies the completed app bundle separately.

### CMake cannot find vcpkg packages

Confirm that `VCPKG_ROOT` points to the bootstrapped vcpkg checkout, rerun the
appropriate `vcpkg install` command, and configure with the vcpkg toolchain file.

### The Linux GUI cannot open a display

Run it inside a desktop session. Xvfb is suitable for automated tests, but not
for interactive use.
