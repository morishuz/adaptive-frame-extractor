# Application icon

`icon.png` is the original supplied artwork, preserved without alteration.

On macOS, CMake runs `scripts/generate_macos_icon.sh` using the system `sips`
tool and the project's deterministic ICNS packer. It generates
`icons/FrameExtractor.iconset` and `icons/FrameExtractor.icns` inside the build
directory. The iconset contains
16, 32, 128, 256, and 512 point representations at both 1x and 2x resolution
(16 through 1024 pixels).

The `.icns` is embedded in `Frame Extractor.app/Contents/Resources` and declared
in the app's `Info.plist`, so development builds and release DMGs use the same
icon. Generated icons stay in the ignored build directory. Replacing the source
PNG or changing the generator automatically regenerates them on the next build.

Windows embeds `FrameExtractor.ico` in the executable, with 16, 24, 32, 48, 64,
128 and 256 pixel representations. Windows and Linux also use the 256 pixel
`FrameExtractor.png` as the SDL window icon. These two derived assets are
checked in so normal builds need no image-processing tools. After changing the
source artwork, regenerate them with Python and Pillow:

```sh
python3 scripts/generate_windows_icon.py
```

Commit both generated files alongside the updated source PNG.
