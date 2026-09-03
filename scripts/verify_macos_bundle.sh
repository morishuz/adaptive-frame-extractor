#!/bin/sh
set -eu

structure_only=0
if [ "${1:-}" = "--structure-only" ]; then
  structure_only=1
  shift
fi

if [ "$#" -ne 1 ]; then
  echo "usage: $0 [--structure-only] APPLICATION.app" >&2
  exit 2
fi

bundle=$1
executable="$bundle/Contents/MacOS/Frame Extractor"
cli="$bundle/Contents/MacOS/frame-extractor"

require_item() {
  if [ ! -e "$1" ]; then
    echo "missing bundle item: $1" >&2
    exit 1
  fi
}

require_item "$bundle/Contents/Info.plist"
require_item "$executable"
require_item "$bundle/Contents/Resources/fonts/InterVariable.ttf"
require_item "$bundle/Contents/Resources/configs/low.yaml"
require_item "$bundle/Contents/Resources/configs/medium.yaml"
require_item "$bundle/Contents/Resources/configs/high.yaml"
require_item "$bundle/Contents/Resources/licenses/LICENSE"
require_item "$bundle/Contents/Resources/licenses/Inter-LICENSE.txt"
require_item "$bundle/Contents/Resources/licenses/THIRD_PARTY_NOTICES.md"
if [ "$structure_only" -eq 0 ]; then
  require_item "$cli"
  require_item "$bundle/Contents/Resources/licenses/ImGui-LICENSE.txt"
  require_item "$bundle/Contents/Resources/licenses/third-party/ffmpeg.txt"
  require_item "$bundle/Contents/Resources/licenses/third-party/opencv4.txt"
  require_item "$bundle/Contents/Resources/licenses/third-party/sdl3.txt"
  require_item "$bundle/Contents/Resources/licenses/third-party/yaml-cpp.txt"
fi

identifier=$(plutil -extract CFBundleIdentifier raw "$bundle/Contents/Info.plist")
version=$(plutil -extract CFBundleShortVersionString raw "$bundle/Contents/Info.plist")
icon=$(plutil -extract CFBundleIconFile raw "$bundle/Contents/Info.plist")
if [ "$icon" != "FrameExtractor.icns" ]; then
  echo "unexpected bundle icon: $icon" >&2
  exit 1
fi
require_item "$bundle/Contents/Resources/$icon"
if [ "$identifier" != "com.drmo.frame-extractor" ]; then
  echo "unexpected bundle identifier: $identifier" >&2
  exit 1
fi

if [ "$structure_only" -eq 0 ]; then
  invalid_dependencies=0
  while IFS= read -r item; do
    if ! file "$item" | grep -q 'Mach-O'; then
      continue
    fi
    while IFS= read -r dependency; do
      case "$dependency" in
        @executable_path/*|@loader_path/*|@rpath/*|/System/Library/*|/usr/lib/*)
          ;;
        *)
          echo "external dependency: $item -> $dependency" >&2
          invalid_dependencies=1
          ;;
      esac
    done <<EOF
$(otool -L "$item" | sed '1d' | sed 's/^[[:space:]]*//' | sed 's/ (.*$//')
EOF
  done <<EOF
$(find "$bundle" -type f)
EOF

  if [ "$invalid_dependencies" -ne 0 ]; then
    exit 1
  fi

  codesign --verify --deep --strict "$bundle"
fi

actual_version=$("$executable" --version)
expected_gui_prefix="frame-extractor-gui v$version ("
case "$actual_version" in
  "$expected_gui_prefix"*")") ;;
  *)
    echo "executable version does not match Info.plist: $actual_version" >&2
    exit 1
    ;;
esac
if [ "$structure_only" -eq 0 ]; then
  cli_version=$("$cli" --version)
  expected_cli_prefix="frame-extractor v$version ("
  case "$cli_version" in
    "$expected_cli_prefix"*")") ;;
    *)
      echo "CLI version does not match Info.plist: $cli_version" >&2
      exit 1
      ;;
  esac
fi

echo "Verified $bundle ($identifier, version $version)"
