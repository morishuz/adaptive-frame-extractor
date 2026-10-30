# Releasing Frame Extractor

This document is for maintainers preparing test packages or a public release.
Source-build instructions are in the [development guide](development.md).

## Continuous integration packages

The CI workflow builds and tests these packages:

- macOS Apple Silicon DMG
- Windows x64 ZIP
- Ubuntu 24.04 x64 `.deb` installer and optional portable `.tar.gz`

Packages from an ordinary CI run appear under **Actions → CI → Artifacts** and
are retained for 14 days. Each platform uploads independently, so a failed run
may contain only the packages whose jobs completed successfully.

CI checks the command-line application, bundled resources and runtime
libraries, a real video extraction, and GUI startup/rendering. These automated
checks do not replace hands-on testing on clean computers.

Linux CI also installs the `.deb` using apt, checks its global launcher/icon,
runs extraction and GUI startup through `/usr/bin`, and removes it again.
The `.deb` keeps binaries, resources, and bundled libraries under
`/opt/frame-extractor`, with launchers in `/usr/bin` and desktop integration in
`/usr/share`. CPack's dependency scanner adds linked system libraries; SDL's
dynamically loaded display and decoration libraries are declared explicitly.

## Tagged releases

Pushing a version tag matching the CMake project version, such as `v0.2.0`,
runs the release workflow and attaches the verified macOS, Windows, and Linux
packages and checksums to a GitHub Release. A release-candidate tag such as
`v0.2.0-rc.1` uses the same application version and creates a GitHub prerelease.
A manual workflow run creates test artifacts without publishing a release.

Use a release candidate for hands-on testing on clean machines. Once the
checklist below passes, create the final version tag from the exact tested
commit; do not move or reuse a published tag.

Ordinary CI builds use an ad-hoc signature. Release builds require Developer ID
signing and notarization and fail if any credentials are missing. Configure these
repository secrets:

- `MACOS_CERTIFICATE`
- `MACOS_CERTIFICATE_PASSWORD`
- `MACOS_CODESIGN_IDENTITY`
- `MACOS_NOTARY_APPLE_ID`
- `MACOS_NOTARY_TEAM_ID`
- `MACOS_NOTARY_PASSWORD`

When all credentials are present, the workflow signs with the hardened runtime,
notarizes the DMG, staples and validates the notarization ticket, and checks
Gatekeeper acceptance and the application team identity. Windows packages remain
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

On Linux, the standalone vcpkg configuration produces both formats with:

```sh
cmake --build build/ci --target package
```

Building `.deb` files additionally requires `dpkg-dev`. Install and remove the
Debian package on clean Ubuntu 24.04, confirming the menu and dock icon appear
without running the portable registration helper. A previously registered
per-user launcher must be removed before testing the system launcher.

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

## Testing macOS signing without publishing

Run **Actions → Package macOS → Run workflow**, leaving `sign` enabled. This
builds only macOS, requires the signing secrets above, and uploads a verified
DMG and SHA-256 checksum without creating a release. Test that DMG on a clean
Mac before publishing. The current package supports Apple Silicon and macOS 14
or newer.

The existing `morishuz/homebrew-tap` can host a `frame-extractor` cask once the
signed DMG is published. The cask must use that release's exact download URL
and final SHA-256 checksum, require ARM64 and macOS Sonoma, and install
`Frame Extractor.app`. Update its version and checksum for each release; do not
point it at temporary Actions artifacts or an unsigned older release.
