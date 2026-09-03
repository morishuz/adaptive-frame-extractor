![Frame Extractor desktop application](images/screenshot.png)

# Frame Extractor

Frame Extractor is a desktop application for selecting representative images
from video. It can find frames automatically from camera motion, sample at a
fixed interval, or save an individual frame chosen in the preview.

The application runs locally: videos do not need to be uploaded to a service.

## Features

- Low, Medium, and High automatic extraction profiles
- Fixed sampling at `1/4`, `1/6`, `1/8`, `1/10`, `1/12`, or `1/16` of the
  source frames
- Frame-accurate navigation and multiple selectable timeline regions
- JPEG or lossless PNG output at the video's original resolution
- Manual extraction of the displayed frame, with duplicate protection
- Optional separate output folders for each selected region
- Live preview, tracking overlay, diagnostic graphs, and progress display
- Cancellation without discarding frames that have already been saved
- A command-line interface for scripted or headless use

## Download

Frame Extractor currently provides test packages for:

- macOS on Apple Silicon as a DMG
- Windows x64 as a ZIP archive
- Ubuntu 24.04 x64 as a tar.gz archive

Tagged [GitHub Releases](https://github.com/morishuz/frame-extractor-desktop/releases)
contain all three packages and their SHA-256 checksums. Packages from ordinary
builds are also available from a successful **Actions → CI** run under
**Artifacts** for 14 days; GitHub may require you to sign in before downloading
workflow artifacts.

These builds are not yet production-signed public releases. On macOS, you may
need to right-click the application and choose **Open** the first time. Extract
Windows and Linux archives completely and keep their `bin`, `lib`, and `share`
directories together.

macOS is the primary tested platform. Windows and Linux packages receive
automated tests, but still need broader hands-on testing before a public
release.

## Using the application

1. Choose or drag a video into the application.
2. Choose where the extracted images should be saved.
3. Select an automatic profile or a fixed frame interval.
4. Optionally use the timeline to define one or more extraction regions.
5. Choose JPEG or PNG and select **Start extraction**.

Low, Medium, and High control how frequently the automatic motion-based mode
selects frames. Fixed interval mode ignores motion and selects an exact ratio
of source frames.

Use the timeline, arrow keys, or the **< Frame** and **Frame >** buttons to move
through the video. Hold the keys or buttons to continue moving. **Set In (I)**
and **Set Out (O)** create a region. Regions are remembered for each input
video; **Clear regions** returns to whole-video extraction.

Select **Extract** to save the frame currently displayed in the preview. Manual
frames are saved at source resolution in a video-specific folder beneath
`manual_frames` in the selected output directory. Extracting the same source
frame again in the same format does not rewrite it.

Every automatic extraction region includes its first and last processed frame.
Use **Cancel** or press Esc to stop a run safely.

## Output

Automatic extraction creates a timestamped directory containing the images,
a CSV manifest, the settings used, and a readable summary:

```text
20260827_120000/
  config.yaml
  keyframes.csv
  summary.txt
  keyframes/
    keyframe_0000_000000.jpg
    ...
```

When **Separate region folders** is enabled, images are grouped into
`keyframes/region_01`, `keyframes/region_02`, and so on. JPEG is the default;
PNG avoids additional lossy compression but creates larger files and takes
longer to encode.

## Command-line interface

After building from source, create an extraction run with:

```sh
./build/release/frame-extractor input.mp4 --output-dir output
```

Pass a profile and process only part of a video with:

```sh
./build/release/frame-extractor input.mp4 \
  --config configs/profiles/medium.yaml \
  --start-frame 1000 \
  --max-frames 500 \
  --output-dir output
```

Run `frame-extractor --help` for all available options. Ctrl-C requests the
same safe cancellation used by the desktop application.

## Build from source

The [local development guide](docs/development.md) contains complete dependency,
build, test, and launch instructions for macOS, Windows, and Ubuntu Linux. A
C++20 compiler, CMake, FFmpeg, OpenCV, SDL3, yaml-cpp, and Catch2 are required.

Maintainer packaging, signing, and platform-validation instructions are in the
[release guide](docs/releasing.md).

## Current limitations

- Automatic sharpness or blur-aware selection is not implemented.
- macOS packages target Apple Silicon rather than Intel Macs.
- Windows and Linux packages have automated coverage but limited hands-on
  validation across different computers and graphics drivers.
- The Linux archive targets Ubuntu 24.04 x64 and is not a universal AppImage.

## Contributing and license

Bug reports and focused contributions are welcome. See
[CONTRIBUTING.md](CONTRIBUTING.md) for the short contribution guide.
Please report security vulnerabilities privately as described in
[SECURITY.md](SECURITY.md).

Frame Extractor is released under the [MIT License](LICENSE). Distributed
packages also contain third-party software covered by the notices in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
