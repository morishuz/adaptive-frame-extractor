#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="${1:-${project_root}/fixtures}"
fixture_tmp="$(mktemp -d)"
trap 'rm -rf "${fixture_tmp}"' EXIT

mkdir -p "${output_dir}"

ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc2=size=96x64:rate=12:duration=1" \
  -c:v libx264 -preset veryslow -crf 18 -pix_fmt yuv420p \
  -g 12 -bf 2 -movflags +faststart -an \
  "${fixture_tmp}/cfr_h264.mp4"

ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc2=size=96x64:rate=12:duration=1" \
  -c:v libx265 -preset slow -crf 22 -pix_fmt yuv420p -tag:v hvc1 \
  -x265-params "log-level=error:keyint=12:min-keyint=12:scenecut=0:pools=1" \
  -movflags +faststart -an \
  "${fixture_tmp}/cfr_hevc.mp4"

ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc=size=95x63:rate=7:duration=1" \
  -c:v libx264 -preset veryslow -crf 18 -pix_fmt yuv444p \
  -g 7 -bf 0 -movflags +faststart -an \
  "${fixture_tmp}/odd_h264.mp4"

ffmpeg -hide_banner -loglevel error -y \
  -display_rotation:v:0 90 -i "${fixture_tmp}/cfr_h264.mp4" \
  -c copy "${fixture_tmp}/rotated_h264.mov"

ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc2=size=160x90:rate=30:duration=10" \
  -c:v libx264 -preset veryslow -crf 24 -pix_fmt yuv420p \
  -g 60 -bf 2 -movflags +faststart -an \
  "${fixture_tmp}/long_cfr_h264.mp4"

ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc=size=32x24:rate=4:duration=1" \
  -vf "format=bgr0" -c:v ffv1 -level 3 -pix_fmt bgr0 -an \
  "${fixture_tmp}/rgb_ffv1.mkv"

ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc2=size=32x24:rate=1:duration=1" \
  -vf "format=yuyv422" -c:v rawvideo -pix_fmt yuyv422 -an \
  "${fixture_tmp}/packed_yuyv422.avi"

ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i "color=c=white@0.5:size=32x24:rate=1:duration=1" \
  -vf "format=ya8" -c:v ffv1 -level 3 -pix_fmt ya8 -an \
  "${fixture_tmp}/gray_alpha_ffv1.mkv"

for depth in 10 12 16; do
  pixel_format="yuv420p${depth}le"
  ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc2=size=32x24:rate=1:duration=1" \
    -vf "format=${pixel_format}" -c:v ffv1 -level 3 \
    -pix_fmt "${pixel_format}" -an \
    "${fixture_tmp}/planar_${depth}bit_ffv1.mkv"
done

python3 - "${fixture_tmp}" "${output_dir}" <<'PY'
import base64
import sys
import textwrap
from pathlib import Path

source_dir = Path(sys.argv[1])
output_dir = Path(sys.argv[2])
for source in sorted(source_dir.iterdir()):
    encoded = base64.b64encode(source.read_bytes()).decode("ascii")
    wrapped = "\n".join(textwrap.wrap(encoded, 100)) + "\n"
    (output_dir / f"{source.name}.b64").write_text(wrapped, encoding="ascii")
PY

echo "Generated regression fixtures in ${output_dir}"
