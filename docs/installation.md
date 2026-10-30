# Installation and first launch

Download the package for your platform from
[GitHub Releases](https://github.com/morishuz/adaptive-frame-extractor/releases).
The [README](../README.md#download) has direct download links.

## macOS (Apple Silicon)

Requires **macOS 14 or newer**. The current macOS preview is Developer ID signed
and notarized by Apple.

With [Homebrew](https://brew.sh):

```sh
brew install --cask morishuz/tap/frame-extractor
```

Or open the [DMG](https://github.com/morishuz/adaptive-frame-extractor/releases/download/v0.2.0-rc.4/Frame-Extractor-0.2.0-macOS-arm64.dmg),
drag **Frame Extractor.app** into **Applications**, and eject the DMG.
Open **Frame Extractor** from Applications.

To update a Homebrew installation:

```sh
brew update
brew upgrade --cask morishuz/tap/frame-extractor
```

To uninstall, run `brew uninstall --cask morishuz/tap/frame-extractor`, or move
a manually installed app from Applications to the Trash.

## Windows (x64)

1. Right-click the downloaded ZIP and choose **Extract All**.
2. Keep the extracted folder together, including its DLLs and resource folders.
3. Open `bin\frame-extractor-gui.exe`. Do not launch it from inside the ZIP.
4. Optionally create a shortcut or pin the running app to the taskbar.

### If Windows blocks the unsigned build

If Microsoft Defender SmartScreen displays **Windows protected your PC**,
click **More info**, check that the app is `frame-extractor-gui.exe`, then
choose **Run anyway** if you trust the download. An unknown publisher is
expected for an unsigned build.
See [Microsoft's SmartScreen guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

If **Run anyway** is unavailable, Windows 11 Smart App Control or an
organization's policy may be blocking the app. Smart App Control has no
per-app exception; on a managed computer, contact your administrator.
See [Microsoft's Smart App Control FAQ](https://support.microsoft.com/en-us/windows/security/threat-malware-protection/smart-app-control-frequently-asked-questions).

To uninstall, close the app and delete its extracted folder and shortcuts.

## Linux (Ubuntu 24.04 x64)

### DEB installer (recommended)

Install the downloaded file with apt, replacing the filename if needed:

```sh
sudo apt install ./frame-extractor_0.2.0_amd64.deb
```

Launch **Frame Extractor** from the applications menu and optionally pin it to
the dock. The DEB installs the launcher, rounded icon, and system dependencies
automatically, including the Wayland decoration library. **No separate
registration script or libdecor installation command is needed.**

Install a newer DEB with the same apt command to update. To uninstall:

```sh
sudo apt remove frame-extractor
```

If you previously registered a portable or development build, remove its user
launcher so it does not override the DEB's system launcher:

```sh
rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/applications/io.github.morishuz.FrameExtractor.desktop"
```

Reopen the app from the applications menu and replace any old dock pin.

### Portable tar.gz (optional)

Extract the archive into a folder you intend to keep, then open
`bin/frame-extractor-gui`. Keep the entire extracted folder together.

Newer archives include a helper to add the applications-menu entry and dock
icon. From the extracted folder, run it once (requires Python 3, no sudo):

```sh
python3 bin/install-desktop-integration.py
```

Launch the registered app from the applications menu. Rerun the helper if you
move the folder or update to a build extracted elsewhere. To uninstall, delete
the extracted folder and remove the user launcher with the command above.

For **portable builds only**, Ubuntu/GNOME Wayland needs these system packages
for native title-bar controls:

```sh
sudo apt install libdecor-0-0 libdecor-0-plugin-1-gtk
```

Restart the app afterwards. Older builds compiled without libdecor support
need a newer build as well. New builds also provide a **Quit** button and
**Ctrl+Q**, with confirmation when extraction is running.

### Release downloads

The **v0.2.0-rc.3** release includes both the recommended DEB installer and the
optional portable archive with its desktop-registration helper. Use the direct
[download links in the README](../README.md#download).
