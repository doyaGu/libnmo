"""The materials of Ballance's levels, as project manifest materials.

The 13 original levels share one set of materials: each name has the same
colors, render settings and texture in every level it appears in.  They are
read from the level files with the nmo CLI and written as the root
"materials" of a project manifest (see docs/ballance-level-format.md), so a
generated level can use the game's own materials by name.

Textures are written as external references to the game's texture files,
with the pixel format the original levels give them.  Settings equal to a
new material's defaults are left out.
"""

from __future__ import annotations

import collections
import json
import sys
from pathlib import Path

from ..core.nmo import Nmo

TEXTURE_BLENDS = {1: "decal", 2: "modulate", 3: "decal_alpha", 4: "modulate_alpha",
                  7: "copy", 8: "add"}
BLEND_FACTORS = {1: "zero", 2: "one", 3: "src_color", 4: "inv_src_color",
                 5: "src_alpha", 6: "inv_src_alpha", 7: "dest_alpha",
                 8: "inv_dest_alpha", 9: "dest_color", 10: "inv_dest_color"}
ALPHA_FUNCS = {1: "never", 2: "less", 3: "equal", 4: "less_equal", 5: "greater",
               6: "not_equal", 7: "greater_equal", 8: "always"}
TEXTURE_FORMATS = {"_32_ARGB8888": "argb8888", "_32_RGB888": "rgb888",
                   "_16_RGB565": "rgb565", "_16_ARGB1555": "argb1555",
                   "_16_ARGB4444": "argb4444"}

# A new CKMaterial's packed settings (RCKMaterial's constructor).
DEFAULT_TEXTURE_BLEND = 4
DEFAULT_SOURCE_BLEND = 2
DEFAULT_DEST_BLEND = 1


def export(nmo: Nmo, path: Path, class_name: str) -> list[dict]:
    """The objects of a class, with their fields by name."""
    return [{"id": o["id"], "name": o["name"], "fields": {f["name"]: f for f in o["fields"]}}
            for o in nmo.objects(path, class_name)]


def color(field: dict) -> list[float]:
    return [round(float(v), 6) for v in field["value"].strip("()").split(",")]


def texture_entry(texture: dict) -> dict:
    fields = texture["fields"]
    names = fields["slot_filenames"].get("items") or []
    entry = {"texture": json.loads(names[0]) if names else texture["name"],
             "texture_save": "external"}
    video = fields.get("desired_video_format", {}).get("value")
    if fields.get("has_desired_video_format", {}).get("value") and video in TEXTURE_FORMATS:
        entry["texture_format"] = TEXTURE_FORMATS[video]
    return entry


def material_entry(material: dict, textures: dict[int, dict]) -> dict:
    fields = material["fields"]
    entry: dict = {"name": material["name"]}
    for key, field in (("diffuse", "diffuse_color"), ("ambient", "ambient_color"),
                       ("specular", "specular_color"), ("emissive", "emissive_color")):
        entry[key] = color(fields[field])
    entry["specular_power"] = round(float(fields["specular_power"]["value"]), 4)

    texture_id = fields["texture_0"]["value"]
    if texture_id in textures:
        entry.update(texture_entry(textures[texture_id]))

    modes = fields["packed_modes"]["value"]
    flags = fields["packed_flags"]["value"]
    blend = {}
    if (modes & 0xF) != DEFAULT_TEXTURE_BLEND:
        blend["texture"] = TEXTURE_BLENDS[modes & 0xF]
    # The blend factors and the alpha function only count while their flag
    # bit is on; the manifest turns the bits on by giving them.
    if flags & 0x08:
        blend["source"] = BLEND_FACTORS[(modes >> 12) & 0xF]
        blend["destination"] = BLEND_FACTORS[(modes >> 16) & 0xF]
    elif ((modes >> 12) & 0xF, (modes >> 16) & 0xF) != (DEFAULT_SOURCE_BLEND, DEFAULT_DEST_BLEND):
        print(f"{material['name']}: blend factors set while blending is off, dropped",
              file=sys.stderr)
    if blend:
        entry["blend"] = blend
    if flags & 0x10:
        entry["alpha"] = {"func": ALPHA_FUNCS[(flags >> 16) & 0xF], "ref": flags >> 24}
    if flags & 0x01:
        entry["two_sided"] = True
    if not flags & 0x02:
        entry["z_write"] = False
    return entry


def extract_materials(levels: list[Path], nmo: Nmo) -> list[dict]:
    """The materials of the levels, one entry each, sorted by name; a material
    that differs between levels as most levels have it (the others are
    reported on stderr)."""
    variants: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    for level in levels:
        textures = {t["id"]: t for t in export(nmo, level, "CKTexture")}
        for material in export(nmo, level, "CKMaterial"):
            entry = material_entry(material, textures)
            variants[entry["name"]][json.dumps(entry, sort_keys=True)] += 1
    materials = []
    for name in sorted(variants):
        counts = variants[name].most_common()
        if len(counts) > 1:
            print(f"{name}: {len(counts)} variants across levels, keeping the one "
                  f"{counts[0][1]} of {sum(c for _, c in counts)} levels use", file=sys.stderr)
        materials.append(json.loads(counts[0][0]))
    return materials
