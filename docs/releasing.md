# Releasing Frame Extractor

This document is for maintainers preparing test packages or a public release.
Source-build instructions are in the [development guide](development.md).

## Continuous integration packages

The CI workflow builds and tests these packages:

- macOS Apple Silicon DMG
- Windows x64 ZIP
- Ubuntu 24.04 x64 tar.gz

Packages from an ordinary CI run appear under **Actions → CI → Artifacts** and
are retained for 14 days. Each platform uploads independently, so a failed run
may contain only the packages whose jobs completed successfully.

CI checks the command-line application, bundled resources and runtime
libraries, a real video extraction, and GUI startup/rendering. These automated
checks do not replace hands-on testing on clean computers.

## Tagged releases

Pushing a version tag matching the CMake project version, such as `v0.2.0`,
runs the release workflow and attaches the verified macOS, Windows, and Linux
packages and checksums to a GitHub Release. A release-candidate tag such as
`v0.2.0-rc.1` uses the same application version and creates a GitHub prerelease.
A manual workflow run creates test artifacts without publishing a release.

Use a release candidate for hands-on testing on clean machines. Once the
checklist below passes, create the final version tag from the exact tested
commit; do not move or reuse a published tag.

Without Apple credentials, the workflow creates an ad-hoc-signed test build.
For public distribution, configure:

- `MACOS_CERTIFICATE`
- `MACOS_CERTIFICATE_PASSWORD`
- `MACOS_CODESIGN_IDENTITY`
- `MACOS_NOTARY_APPLE_ID`
- `MACOS_NOTARY_TEAM_ID`
- `MACOS_NOTARY_PASSWORD`

When all credentials are present, the workflow signs with the hardened runtime,
notarizes the DMG, and staples the notarization ticket. Windows packages remain
unsigned until a trusted code-signing certificate is configured; Linux archives
do not use platform code signing.

## Local package validation

Enable the GUI smoke test when configuring a release build:

```sh
cmake --preset release -DFRAME_EXTRACTOR_GUI_SMOKE_TEST=ON
cmake --build --preset release
ctest --preset release --output-on-failure
```

Retain any platform-specific dependency or toolchain arguments from the
[development guide](development.md). On headless Linux, run the tests through
Xvfb:

```sh
SDL_VIDEO_DRIVER=x11 SDL_RENDER_DRIVER=software \
  xvfb-run -a ctest --test-dir build/release --output-on-failure
```

## Manual release checklist

Test the packaged application on a clean machine without development libraries:

- Launch it from a path containing spaces and non-ASCII characters.
- Check the icon, font, resizing, display scaling, and high-DPI rendering.
- Choose and drag in videos; test the native input and output dialogs.
- Load a second video and scrub repeatedly in both directions.
- Test automatic profiles, fixed intervals, manual extraction, and JPEG/PNG.
- Extract the whole video and multiple regions, including separate folders.
- Confirm boundary frames, summaries, manifests, and duplicate protection.
- Cancel a longer run and inspect the completed output.
- Open the run directory and summary from the application.
- Test H.264, HEVC, variable frame rate, rotation, and odd dimensions.
- On Linux, test both X11 and Wayland when possible.

Before a public release, confirm the resolved FFmpeg features and review the
bundled dependency licences described in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). Windows installers,
universal Linux packaging, and broad hardware/graphics validation remain
separate release tasks.
