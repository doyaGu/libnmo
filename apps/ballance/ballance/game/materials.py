"""The materials of the original levels, as project manifest materials
(apps/ballance/data/level_materials.json, written by
ballance_level_materials.py)."""

from __future__ import annotations

import json
from pathlib import Path

PRESETS = Path(__file__).resolve().parents[2] / "data" / "level_materials.json"


def load_presets(path: Path = PRESETS) -> list[dict]:
    return json.loads(path.read_text(encoding="utf-8"))["materials"]


def textures_by_material(path: Path = PRESETS) -> dict[str, str | None]:
    """Each material's texture file name."""
    return {m["name"]: m.get("texture") for m in load_presets(path)}
