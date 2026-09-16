#!/usr/bin/env python3
"""Regenerate portable icons; requires Pillow only when changing the source artwork."""

from pathlib import Path

from PIL import Image, ImageDraw


if __name__ == "__main__":
    icons = Path(__file__).resolve().parents[1] / "assets" / "icons"
    with Image.open(icons / "icon.png") as source:
        if source.width != source.height or source.width < 1024:
            raise ValueError("Source icon must be square and at least 1024 pixels wide")
        artwork = source.convert("RGBA")
        artwork.save(
            icons / "FrameExtractor.ico",
            sizes=[(size, size) for size in (16, 24, 32, 48, 64, 128, 256)],
        )
        # Match the existing rounded tile, removing only its white exterior.
        # Draw at source resolution so downsampling antialiases the alpha edge.
        scale = artwork.width / 256
        mask = Image.new("L", artwork.size, 0)
        ImageDraw.Draw(mask).rounded_rectangle(
            (10 * scale, 10 * scale, 245 * scale, 245 * scale),
            radius=48 * scale,
            fill=255,
        )
        artwork.putalpha(mask)
        artwork.resize((256, 256), Image.Resampling.LANCZOS).save(icons / "FrameExtractor.png")
