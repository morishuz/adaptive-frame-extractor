#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
  echo "usage: $0 SOURCE.png OUTPUT.icns ICNS_BUILDER" >&2
  exit 2
fi

source_image=$1
output=$2
icns_builder=$3
case "$output" in
  *.icns) iconset="${output%.icns}.iconset" ;;
  *) echo "output must have an .icns extension" >&2; exit 2 ;;
esac

dimensions=$(sips -g pixelWidth -g pixelHeight "$source_image")
width=$(printf '%s\n' "$dimensions" | awk '/pixelWidth:/ {print $2}')
height=$(printf '%s\n' "$dimensions" | awk '/pixelHeight:/ {print $2}')
if [ "$width" != "$height" ] || [ "${width:-0}" -lt 1024 ]; then
  echo "source icon must be square and at least 1024 pixels wide" >&2
  exit 1
fi

mkdir -p "$iconset"
for points in 16 32 128 256 512; do
  for scale in 1 2; do
    pixels=$((points * scale))
    suffix=""
    if [ "$scale" -eq 2 ]; then
      suffix="@2x"
    fi
    sips --resampleHeightWidth "$pixels" "$pixels" "$source_image" \
      --out "$iconset/icon_${points}x${points}${suffix}.png" >/dev/null
  done
done

"$icns_builder" "$output" "$iconset"
