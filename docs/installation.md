# Installation and first launch

Download the package for your platform from
[GitHub Releases](https://github.com/morishuz/adaptive-frame-extractor/releases).
The [README](../README.md#download) has direct download and SHA-256 links.
For unsigned test builds, approve an app-specific launch prompt only if you
downloaded the app from this project and trust that build.

## macOS (Apple Silicon)

1. Open the downloaded DMG.
2. Drag **Frame Extractor.app** into **Applications**.
3. Eject the DMG and open Frame Extractor from Applications.

### If macOS blocks the unsigned build

If macOS says the developer cannot be verified or Apple cannot check the app:

1. Try opening the app once, then dismiss the warning.
2. Open **System Settings → Privacy & Security**.
3. Find the notice for Frame Extractor and click **Open Anyway**.
4. Authenticate if asked, then confirm **Open**.

macOS saves this exception so you can open the app normally afterwards.
See [Apple's instructions](https://support.apple.com/en-gb/102445).
If the message instead says the app is damaged or will damage your computer,
download a fresh copy and report the exact message rather than disabling
Gatekeeper globally.

To uninstall, move Frame Extractor.app from Applications to the Trash.

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

### Getting the newer Linux builds

The published **v0.2.0-rc.2** release contains only the older portable archive;
it does not include the DEB or the desktop-registration helper.
Until a newer release is published, use the `frame-extractor-linux-x64` artifact
from a successful [CI run](https://github.com/morishuz/adaptive-frame-extractor/actions/workflows/ci.yml)
containing the Debian packaging changes. Extract the artifact ZIP to find the
DEB, portable archive, and checksums. GitHub requires sign-in for artifact
downloads; artifacts are retained for 14 days.
