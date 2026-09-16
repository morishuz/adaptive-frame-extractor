#!/usr/bin/env python3
"""Register an extracted Linux package in the current user's applications menu."""

import os
from pathlib import Path


def desktop_string(value):
    return (str(value).replace("\\", "\\\\").replace("\n", "\\n")
            .replace("\r", "\\r").replace("\t", "\\t"))


def desktop_exec(path):
    # Exec quoting is applied before desktop-entry string escaping.
    value = str(path).replace("%", "%%")
    for character in ('\\', '"', '`', '$'):
        value = value.replace(character, '\\' + character)
    return desktop_string('"' + value + '"')


def main():
    package = Path(__file__).resolve().parent.parent
    app_id = "io.github.morishuz.FrameExtractor"
    executable = package / "bin/frame-extractor-gui"
    icon = package / "bin/icons/FrameExtractor.png"
    template = package / f"share/applications/{app_id}.desktop"
    for resource in (executable, icon, template):
        if not resource.is_file():
            raise SystemExit(f"Missing package resource: {resource}")

    data_home = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")
    if not data_home.is_absolute():
        raise SystemExit("XDG_DATA_HOME must be an absolute path")
    applications = data_home / "applications"
    applications.mkdir(parents=True, exist_ok=True)
    destination = applications / f"{app_id}.desktop"
    entry = template.read_text(encoding="utf-8")
    # Keep the executable token fixed: GLib checks it before expanding %%.
    entry = entry.replace("Exec=frame-extractor-gui %f",
                          f"Exec=/usr/bin/env {desktop_exec(executable)} %f")
    entry = entry.replace(f"Icon={app_id}", f"Icon={desktop_string(icon)}")
    destination.write_text(entry, encoding="utf-8")
    print(f"Installed launcher: {destination}")
    print("Launch Frame Extractor from your applications menu, then pin it to the dock.")
    print("Keep this package in place; rerun this script if you move it.")


if __name__ == "__main__":
    main()
