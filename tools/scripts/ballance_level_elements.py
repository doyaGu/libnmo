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
the markers.  Level_03's are cut out of the floors they are part of: the
octagon under its start, which opens to a road on its +X side, and the round
floor with two flame bowls under its first checkpoint, which opens on both X
sides.  They are written as Floor_Start.obj and Floor_Checkpoint.obj, around
the marker: placed at the marker's position, unturned, they stand as in
Level_03.

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
# The floors the original start and first checkpoint stand on: (level, floor,
# marker, half extents along X and Z of the part under the marker).
FLOORS = {"Floor_Start": ("Level_03.NMO", "A01_Floor_01", "PS_FourFlames_01", 8.07, 9.3),
          "Floor_Checkpoint": ("Level_03.NMO", "A02_Floor_01", "PC_TwoFlames_01", 4.93, 9.3)}


def kind_of(name: str) -> str | None:
    if name == "SkyLayer":
        return name
    for kind in MARKERS + ELEMENTS:
        if re.fullmatch(re.escape(kind) + r"_\d+", name):
            return kind
    return None


def nmo_run(nmo: str, *args: str) -> str:
    return subprocess.run([nmo, *args], check=True, capture_output=True, text=True).stdout


def world_matrix(fields: dict) -> list[float]:
    return [float(n) for n in re.findall(r"-?[\d.]+(?:e-?\d+)?", fields["world_matrix"])]


def export_cut(nmo: str, level: Path, mesh_id: int, floor: list[float], marker: list[float],
               half_x: float, half_z: float, kind: str, out: Path) -> None:
    """Writes the faces of a floor's mesh within half_x/half_z of the marker,
    moved so the marker is at the origin."""
    def point(m: list[float], x: float, y: float, z: float, w: float) -> tuple:
        return tuple(x * m[i] + y * m[4 + i] + z * m[8 + i] + w * m[12 + i] for i in range(3))

    with tempfile.TemporaryDirectory() as work:
        nmo_run(nmo, "mesh", "export", "--id", str(mesh_id), "-d", work, str(level))
        obj = next(Path(work).glob("*.obj"))
        mtl = next(Path(work).glob("*.mtl"))
        vs, vts, vns = [], [], []
        lines = [f"mtllib {kind}.mtl"]
        material = None
        for line in obj.read_text(encoding="utf-8").splitlines():
            tokens = line.split()
            if not tokens:
                continue
            if tokens[0] == "v":
                x, y, z = point(floor, *map(float, tokens[1:4]), 1.0)
                vs.append((x - marker[12], y - marker[13], z - marker[14]))
            elif tokens[0] == "vt":
                vts.append(tokens[1:3])
            elif tokens[0] == "vn":
                vns.append(point(floor, *map(float, tokens[1:4]), 0.0))
            elif tokens[0] == "usemtl":
                material = tokens[1]
            elif tokens[0] == "f":
                refs = [[int(n) for n in t.split("/")] for t in tokens[1:]]
                if not all(abs(vs[r[0] - 1][0]) <= half_x and abs(vs[r[0] - 1][2]) <= half_z
                           for r in refs):
                    continue
                if material is not None:
                    lines.append(f"usemtl {material}")
                    material = None
                for v, vt, vn in refs:
                    lines.append("v %.6f %.6f %.6f" % vs[v - 1])
                    lines.append("vt %s %s" % tuple(vts[vt - 1]))
                    lines.append("vn %.6f %.6f %.6f" % vns[vn - 1])
                lines.append("f " + " ".join(f"{i}/{i}/{i}" for i in range(-len(refs), 0)))
        (out / f"{kind}.mtl").write_text(mtl.read_text(encoding="utf-8"), encoding="utf-8")
        (out / f"{kind}.obj").write_text("\n".join(lines) + "\n", encoding="utf-8")


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
    for kind, (level_name, floor_name, marker_name, half_x, half_z) in FLOORS.items():
        level = by_name.get(level_name.lower())
        if level is None:
            continue
        exported = json.loads(nmo_run(
            args.nmo, "-f", "json", "object", "export", "--class", "CK3dObject", str(level)))
        objects = {obj["name"]: {f["name"]: f.get("value") for f in obj["fields"]}
                   for obj in exported["data"]["objects"]}
        if floor_name in objects and marker_name in objects:
            floor = objects[floor_name]
            export_cut(args.nmo, level, floor["current_mesh"], world_matrix(floor),
                       world_matrix(objects[marker_name]), half_x, half_z, kind, out)
            found[kind] = f"{level.name}:{floor_name}"
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
