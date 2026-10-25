"""Render a PC review sheet of every downloaded team logo before ESP upload."""
from __future__ import annotations

import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
LOGOS = ROOT / "firmware" / "data" / "logos"
manifest = json.loads((LOGOS / "manifest.json").read_text(encoding="utf-8"))

canvas = Image.new("RGB", (1600, 1180), "#080d14")
draw = ImageDraw.Draw(canvas)
font_big = ImageFont.truetype("arial.ttf", 32)
font = ImageFont.truetype("arial.ttf", 20)
small = ImageFont.truetype("arial.ttf", 16)


def section(title: str, entries: list[dict], origin_x: int, origin_y: int) -> None:
    draw.text((origin_x, origin_y), title, fill="#eef4ff", font=font_big)
    for index, entry in enumerate(entries):
        col, row = index % 4, index // 4
        x, y = origin_x + col * 195, origin_y + 55 + row * 215
        draw.rounded_rectangle((x, y, x + 175, y + 190), radius=12, fill="#101823", outline="#2a374b", width=2)
        image = Image.open(LOGOS / entry["file"]).convert("RGBA")
        image.thumbnail((130, 130), Image.Resampling.LANCZOS)
        px, py = x + (175 - image.width) // 2, y + 10 + (130 - image.height) // 2
        canvas.paste(image, (px, py), image)
        draw.text((x + 12, y + 145), entry["code"], fill="#d8ff18" if title == "ELH · LITVÍNOV" else "#e11d48", font=font)
        label = entry["name"]
        draw.text((x + 12, y + 170), label[:22], fill="#eef4ff", font=small)


section("ELH · LITVÍNOV", manifest["hockey"], 30, 25)
section("CHANCE LIGA · SPARTA", manifest["football"], 830, 25)
canvas.save(ROOT / "assets" / "logo-preview-2026-27.png")
print(ROOT / "assets" / "logo-preview-2026-27.png")
