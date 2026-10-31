#!/usr/bin/env python3
"""Export the meshes of Ballance's level elements as OBJ files.

A level file places its start, checkpoints, reset points, finish and elements
as objects with meshes of their own, and the game uses some of these meshes
as they are: the start (PS_FourFlames) and the checkpoints (PC_TwoFlames)
have no mesh in their 3D Entities/PH templates, so a level draws them with
its own.  This script takes one mesh of each kind from the original levels,
and the SkyLayer cloud plane every level has, and writes them as <Kind>.obj
(with .mtl) for a project manifest to use.

The start platform and the checkpoint's floor are floors of the level, not
the markers: Level_01 has them as A01_Floor_01 under its start and
A02_Floor_01 under its first checkpoint, each 1.24 below the marker.  They
are written as Floor_Start.obj and Floor_Checkpoint.obj.

The meshes are the game's own, so they are written from a local installation
and not kept with libnmo.  Each is in the object's local space; the original
objects are not scaled.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

MARKERS = ["PS_FourFlames", "PC_TwoFlames", "PR_Resetpoint", "PE_Balloon"]
ELEMENTS = [
    "P_Extra_Life", "P_Extra_Point", "P_Trafo_Paper", "P_Trafo_Stone", "P_Trafo_Wood",
    "P_Ball_Paper", "P_Ball_Stone", "P_Ball_Wood", "P_Box", "P_Dome",
    "P_Modul_01", "P_Modul_03", "P_Modul_08", "P_Modul_17", "P_Modul_18", "P_Modul_19",
    "P_Modul_25", "P_Modul_26", "P_Modul_29", "P_Modul_30", "P_Modul_34", "P_Modul_37",
    "P_Modul_41",
]
KINDS = MARKERS + ELEMENTS + ["SkyLayer"]
# The floors the original start and first checkpoint stand on.
FLOORS = {"Floor_Start": ("Level_01.NMO", "A01_Floor_01"),
          "Floor_Checkpoint": ("Level_01.NMO", "A02_Floor_01")}


def kind_of(name: str) -> str | None:
    if name == "SkyLayer":
        return name
    for kind in MARKERS + ELEMENTS:
        if re.fullmatch(re.escape(kind) + r"_\d+", name):
            return kind
    return None


def nmo_run(nmo: str, *args: str) -> str:
    return subprocess.run([nmo, *args], check=True, capture_output=True, text=True).stdout


def export_mesh(nmo: str, level: Path, mesh_id: int, kind: str, out: Path) -> None:
    with tempfile.TemporaryDirectory() as work:
        nmo_run(nmo, "mesh", "export", "--id", str(mesh_id), "-d", work, str(level))
        obj = next(Path(work).glob("*.obj"))
        mtl = next(Path(work).glob("*.mtl"), None)
        text = obj.read_text(encoding="utf-8")
        if mtl is not None:
            text = text.replace(f"mtllib {mtl.name}", f"mtllib {kind}.mtl")
            (out / f"{kind}.mtl").write_text(mtl.read_text(encoding="utf-8"), encoding="utf-8")
        (out / f"{kind}.obj").write_text(text, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export Ballance's level element meshes as OBJ files.")
    parser.add_argument("game", help="Ballance installation folder (holds '3D Entities')")
    parser.add_argument("-o", "--output", required=True, help="Folder for <Kind>.obj files")
    parser.add_argument("--nmo", default=os.environ.get("NMO", "nmo"),
                        help="nmo CLI to run (default: $NMO or nmo on PATH)")
    args = parser.parse_args()

    levels = sorted((Path(args.game) / "3D Entities" / "Level").glob("Level_*.NMO"))
    if not levels:
        print(f"no Level_*.NMO under {args.game}", file=sys.stderr)
        return 2
    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)

    found: dict[str, str] = {}
    by_name = {level.name.lower(): level for level in levels}
    for kind, (level_name, object_name) in FLOORS.items():
        level = by_name.get(level_name.lower())
        if level is None:
            continue
        exported = json.loads(nmo_run(
            args.nmo, "-f", "json", "object", "export", "--class", "CK3dObject", str(level)))
        for obj in exported["data"]["objects"]:
            if obj["name"] == object_name:
                fields = {f["name"]: f.get("value") for f in obj["fields"]}
                export_mesh(args.nmo, level, fields["current_mesh"], kind, out)
                found[kind] = f"{level.name}:{object_name}"
    for level in levels:
        exported = json.loads(nmo_run(
            args.nmo, "-f", "json", "object", "export", "--class", "CK3dObject", str(level)))
        for obj in exported["data"]["objects"]:
            kind = kind_of(obj["name"])
            if kind is None or kind in found:
                continue
            fields = {f["name"]: f.get("value") for f in obj["fields"]}
            mesh = fields.get("current_mesh")
            if not mesh:
                continue
            export_mesh(args.nmo, level, mesh, kind, out)
            found[kind] = f"{level.name}:{obj['name']}"
        if all(kind in found for kind in KINDS):
            break

    for kind in KINDS + list(FLOORS):
        print(f"{kind:16s} {found.get(kind, 'not found')}")
    return 0 if len(found) == len(KINDS) + len(FLOORS) else 1


if __name__ == "__main__":
    sys.exit(main())
