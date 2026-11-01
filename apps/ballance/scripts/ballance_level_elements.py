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
Level_03.  So is the fan's housing (Floor_Modul_18.obj), the 5 by 5 pit with
a grille the fan blows through, set into the middle of an area 0.35 above it.

The scenery is cut out of Level_01: one of the pillars beside its start, with
the shaft that fades out below it, as Pillar.obj; the lantern tower on top of
that pillar as Tower.obj; and one of the striped poles by its balloon as
Stick.obj.  Pillar and Tower have the middle of the pillar's top at their
origin, Stick the middle of the pole's top.

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
          "Floor_Checkpoint": ("Level_03.NMO", "A02_Floor_01", "PC_TwoFlames_01", 4.93, 9.3),
          "Floor_Modul_18": ("Level_03.NMO", "A05_Floor_02", "P_Modul_18_03", 2.52, 2.52)}
# The scenery: one pillar of Level_01's start (its shaft fading below, too),
# the lantern tower on top of it, and a striped pole by its balloon: (level,
# objects, a point near its middle on X and Z, how far it reaches from there).
# Pillar and Stick are written with their tops' middles at the origin, Tower
# with Pillar's.
DECOR = {"Pillar": ("Level_01.NMO", ["A01_Floor_Columns_Top", "A01_Floor_Colums_Bottom"],
                    (31.8, -167.7), 3.0),
         "Tower": ("Level_01.NMO", ["A01_Tower"], (31.8, -167.7), 4.5),
         "Stick": ("Level_01.NMO", ["A04_Stick_Top", "A04_Stick_Bottom"], (680.2, -889.6), 1.5)}


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


def world_faces(nmo: str, level: Path, mesh_id: int, matrix: list[float]) -> tuple[list, str]:
    """The faces of a mesh placed by an object's world matrix, as (material,
    [(position, uv, normal), ...]), and the mesh's .mtl text."""
    def point(x: float, y: float, z: float, w: float) -> tuple:
        return tuple(x * matrix[i] + y * matrix[4 + i] + z * matrix[8 + i] + w * matrix[12 + i]
                     for i in range(3))

    with tempfile.TemporaryDirectory() as work:
        nmo_run(nmo, "mesh", "export", "--id", str(mesh_id), "-d", work, str(level))
        obj = next(Path(work).glob("*.obj"))
        mtl = next(Path(work).glob("*.mtl")).read_text(encoding="utf-8")
        vs, vts, vns, faces = [], [], [], []
        material = None
        for line in obj.read_text(encoding="utf-8").splitlines():
            tokens = line.split()
            if not tokens:
                continue
            if tokens[0] == "v":
                vs.append(point(*map(float, tokens[1:4]), 1.0))
            elif tokens[0] == "vt":
                vts.append(tuple(tokens[1:3]))
            elif tokens[0] == "vn":
                vns.append(point(*map(float, tokens[1:4]), 0.0))
            elif tokens[0] == "usemtl":
                material = tokens[1]
            elif tokens[0] == "f":
                refs = [[int(n) for n in t.split("/")] for t in tokens[1:]]
                faces.append((material, [(vs[v - 1], vts[vt - 1], vns[vn - 1]) for v, vt, vn in refs]))
    return faces, mtl


def write_faces(faces: list, mtls: list[str], center: tuple[float, float, float], kind: str,
                out: Path) -> None:
    """Writes faces moved so center is at the origin, as <kind>.obj and .mtl."""
    lines = [f"mtllib {kind}.mtl"]
    material = None
    for face_material, corners in faces:
        if face_material != material:
            lines.append(f"usemtl {face_material}")
            material = face_material
        for position, uv, normal in corners:
            lines.append("v %.6f %.6f %.6f" % tuple(position[i] - center[i] for i in range(3)))
            lines.append("vt %s %s" % uv)
            lines.append("vn %.6f %.6f %.6f" % normal)
        lines.append("f " + " ".join(f"{i}/{i}/{i}" for i in range(-len(corners), 0)))
    blocks, seen = [], set()
    for mtl in mtls:
        for block in mtl.split("newmtl ")[1:]:
            name = block.split()[0]
            if name not in seen:
                seen.add(name)
                blocks.append("newmtl " + block.rstrip() + "\n")
    (out / f"{kind}.mtl").write_text("\n".join(blocks), encoding="utf-8")
    (out / f"{kind}.obj").write_text("\n".join(lines) + "\n", encoding="utf-8")


def cut(faces: list, center: tuple[float, float], half_x: float, half_z: float) -> list:
    """The faces lying within half_x and half_z of center, on X and Z."""
    return [(m, corners) for m, corners in faces
            if all(abs(p[0] - center[0]) <= half_x and abs(p[2] - center[1]) <= half_z
                   for p, _, _ in corners)]


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
    centers: dict[str, tuple[float, float, float]] = {}
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
            marker = world_matrix(objects[marker_name])
            faces, mtl = world_faces(args.nmo, level, floor["current_mesh"], world_matrix(floor))
            write_faces(cut(faces, (marker[12], marker[14]), half_x, half_z), [mtl],
                        (marker[12], marker[13], marker[14]), kind, out)
            found[kind] = f"{level.name}:{floor_name}"
    for kind, (level_name, names, near, radius) in DECOR.items():
        level = by_name.get(level_name.lower())
        if level is None:
            continue
        exported = json.loads(nmo_run(
            args.nmo, "-f", "json", "object", "export", "--class", "CK3dObject", str(level)))
        objects = {obj["name"]: {f["name"]: f.get("value") for f in obj["fields"]}
                   for obj in exported["data"]["objects"]}
        if not all(name in objects for name in names):
            continue
        faces, mtls = [], []
        for name in names:
            part, mtl = world_faces(args.nmo, level, objects[name]["current_mesh"],
                                    world_matrix(objects[name]))
            faces += cut(part, near, radius, radius)
            mtls.append(mtl)
        if kind == "Tower":
            center = centers["Pillar"]
        else:
            # the middle of its shaft, at its top
            xs = [p[0] for _, corners in faces for p, _, _ in corners]
            zs = [p[2] for _, corners in faces for p, _, _ in corners]
            ys = [p[1] for _, corners in faces for p, _, _ in corners]
            center = ((min(xs) + max(xs)) / 2, max(ys), (min(zs) + max(zs)) / 2)
            centers[kind] = center
        write_faces(faces, mtls, center, kind, out)
        found[kind] = f"{level.name}:{'+'.join(names)}"
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

    for kind in KINDS + list(FLOORS) + list(DECOR):
        print(f"{kind:16s} {found.get(kind, 'not found')}")
    return 0 if len(found) == len(KINDS) + len(FLOORS) + len(DECOR) else 1


if __name__ == "__main__":
    sys.exit(main())
