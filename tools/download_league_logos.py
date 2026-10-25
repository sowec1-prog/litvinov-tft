"""Download the current 2026/27 ELH and Chance Liga logo sets from league sites."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "firmware" / "data" / "logos"

# Extracted from the current official competition tables on 2026-10-25.
HOCKEY = {
    "CEB": ("Banes Motor České Budějovice", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2F4zj2z-ceb.png&w=240"),
    "HKR": ("Mountfield HK", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2Fj9qyt-hkr.png&w=240"),
    "KVA": ("BYD Energie Karlovy Vary", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2Fhokejv-byd.png&w=240"),
    "KLA": ("Rytíři Kladno", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2F7wb9o-kla.png&w=240"),
    "KOM": ("HC Kometa Brno", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2F8oo52-kom.png&w=240"),
    "LIB": ("Bílí Tygři Liberec", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2Fi5lg4-lib.png&w=240"),
    "LIT": ("HC VERVA Litvínov", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2Fifboo-lit.png&w=240"),
    "MBL": ("BK Mladá Boleslav", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2Fk1yj3-mbl.png&w=240"),
    "OLO": ("HC Olomouc", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2Fy4tmv-olo.png&w=240"),
    "PCE": ("HC Dynamo Pardubice", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2FPCE-2x.png&w=240"),
    "PLZ": ("HC Škoda Plzeň", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2Fpijgb-plz.png&w=240"),
    "SPA": ("HC Sparta Praha", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2Fwueb1-spa.png&w=240"),
    "TRI": ("HC Oceláři Třinec", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2Fccek3-tri.png&w=240"),
    "VIT": ("HC VÍTKOVICE RIDERA", "https://www.hokej.cz/min.php?file=%2Ffiles%2Flogos%2F65gcx-vit.png&w=240"),
}

FOOTBALL = {
    "ART": ("SK Artis Brno", 132), "BOH": ("Bohemians Praha 1905", 57),
    "FCB": ("FC Baník Ostrava", 6), "FCS": ("1. FC Slovácko", 4),
    "FCZ": ("FC Zlín", 36), "FKJ": ("FK Jablonec", 12),
    "FKP": ("FK Pardubice", 97), "HKR": ("FC Hradec Králové", 7),
    "LIB": ("FC Slovan Liberec", 8), "MBL": ("FK Mladá Boleslav", 14),
    "PLZ": ("FC Viktoria Plzeň", 9), "SIG": ("SK Sigma Olomouc", 17),
    "SKS": ("SK Slavia Praha", 89), "SPA": ("AC Sparta Praha", 5),
    "TEP": ("FK Teplice", 15), "ZBR": ("FC Zbrojovka Brno", 11),
}


def fetch(url: str) -> bytes:
    request = Request(url, headers={"User-Agent": "Litvinov-TFT-logo-sync/1.0"})
    with urlopen(request, timeout=30) as response:
        data = response.read()
        if response.status != 200 or len(data) < 100:
            raise RuntimeError(f"download failed: HTTP {response.status}, {len(data)} B")
        if not data.startswith(b"\x89PNG\r\n\x1a\n"):
            raise RuntimeError("expected PNG image data")
        return data


def save(league: str, code: str, name: str, url: str) -> dict:
    data = fetch(url)
    path = OUT / league / f"{code}.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return {"code": code, "name": name, "file": str(path.relative_to(OUT)).replace("\\", "/"), "source_url": url, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main() -> None:
    manifest = {"season": "2026/27", "sources": {"hockey": "https://hokej.cz/tipsport-extraliga", "football": "https://www.chanceliga.cz/tabulka/2027/serie?id_stage=1"}, "hockey": [], "football": []}
    for code, (name, url) in HOCKEY.items():
        manifest["hockey"].append(save("hockey", code, name, url))
    for code, (name, club_id) in FOOTBALL.items():
        # Chance Liga publishes the official crest endpoint at 40×40; keep originals as delivered.
        url = f"https://www.chanceliga.cz/photo/club/40x40/club_{club_id}.png?4"
        manifest["football"].append(save("football", code, name, url))
    (OUT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Downloaded {len(manifest['hockey'])} ELH and {len(manifest['football'])} Chance Liga logos.")


if __name__ == "__main__":
    main()
