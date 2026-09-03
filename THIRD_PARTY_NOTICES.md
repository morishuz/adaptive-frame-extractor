# Third-Party Dependency Notes

This repository's own source is MIT licensed. A distributed binary also contains or links to third-party software whose notices must accompany release artifacts.

| Dependency | Purpose | Upstream license consideration |
|---|---|---|
| OpenCV | DIS optical flow, image processing, image encoding | Apache License 2.0 |
| FFmpeg/libav | Demuxing, decoding, timestamp handling, color conversion | LGPL 2.1+ by default; enabled components and build flags can make a particular build GPL |
| yaml-cpp | Configuration parsing and serialization | MIT License |
| SDL3 | Desktop windows, rendering, input, and native file dialogs | zlib License |
| Dear ImGui | Desktop controls and layout | MIT License |
| Inter 4.1 | Desktop interface font | SIL Open Font License 1.1 |
| Catch2 | Development and tests only | Boost Software License 1.0 |

Before shipping an installer or application bundle:

1. Freeze the exact dependency builds and enabled FFmpeg features.
2. Generate a complete license/notices directory from those resolved artifacts.
3. Confirm that no GPL-only FFmpeg component is enabled unless that distribution choice is intentional.
4. Bundle the corresponding license texts and satisfy the applicable dynamic-linking/source-offer requirements.

Standalone builds use vcpkg's minimal FFmpeg feature selection and dynamic
linking, then include the resolved runtime libraries and these notes in the
package. Copyright and license files for every package in the resolved vcpkg
tree are copied to `Resources/licenses/third-party` inside the macOS bundle,
or `share/frame-extractor/licenses/third-party` on Windows/Linux. Inter and
Dear ImGui license texts are included separately. Windows packages also include
the MSVC runtime redistributables supplied by the build toolchain. The ordinary
Homebrew CPack archive remains a developer artifact; it is not a standalone
distribution.
