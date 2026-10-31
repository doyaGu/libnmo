#!/usr/bin/env python3
"""Check a Ballance level file against the rules of docs/ballance-level-format.md.

Reads the level with the nmo CLI and reports, by severity:

  error    the level does not play: no start or end, reset points and
           checkpoints that do not add up, no floors, no fall zone group
  warning  it plays but something is off: falls never detected, an element
           in no sector, a visible fall zone, a start or checkpoint without
           its geometry, no sky
  info     what the game reports and gets over, such as a missing element
           group (one load-time error, like the original levels)

Exits with 1 when there are errors, 0 otherwise.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

LEVEL_CLASSES = {"CK3dObject", "CKMesh", "CKMaterial", "CKTexture", "CKGroup"}
# Levelinit's PH_Groups: the element kinds, each a template in 3D Entities/PH.
ELEMENT_KINDS = {
    "P_Extra_Life", "P_Extra_Point", "P_Trafo_Paper", "P_Trafo_Stone", "P_Trafo_Wood",
    "P_Ball_Paper", "P_Ball_Stone", "P_Ball_Wood", "P_Box", "P_Dome",
    "P_Modul_01", "P_Modul_03", "P_Modul_08", "P_Modul_17", "P_Modul_18", "P_Modul_19",
    "P_Modul_25", "P_Modul_26", "P_Modul_29", "P_Modul_30", "P_Modul_34", "P_Modul_37",
    "P_Modul_41",
}
SOUND_GROUPS = [f"Sound_{kind}ID_0{i}" for i in (1, 2, 3) for kind in ("Roll", "Hit")]
# Groups the game looks up by name; without one it logs a single error.
NAMED_GROUPS = ["Phys_FloorRails", "Phys_FloorStopper", "Shadow"] + SOUND_GROUPS
# A box: the 8 corners, or 24 vertices with a normal per face.
PLACEHOLDER_VERTICES = 24
# PR_Resetpoint_{N+1} stands where PC_TwoFlames_N does.
CHECKPOINT_TOLERANCE = 1.0


class Report:
    def __init__(self) -> None:
        self.items: list[dict] = []

    def add(self, severity: str, code: str, message: str) -> None:
        self.items.append({"severity": severity, "code": code, "message": message})

    def count(self, severity: str) -> int:
        return sum(1 for item in self.items if item["severity"] == severity)


def nmo_json(nmo: str, *args: str) -> dict:
    result = subprocess.run([nmo, "-f", "json", *args], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"nmo {' '.join(args[:2])} failed: {result.stderr.strip()}")
    return json.loads(result.stdout)["data"]


def numbered(names: list[str], prefix: str) -> list[int] | None:
    """The numbers of names prefix_NN, or None when one does not follow it."""
    numbers = []
    for name in names:
        match = re.fullmatch(re.escape(prefix) + r"_(\d+)", name)
        if match is None:
            return None
        numbers.append(int(match.group(1)))
    return sorted(numbers)


def element_kind(name: str) -> str | None:
    """P_Box_03 -> P_Box, P_Modul_18_07 -> P_Modul_18."""
    for kind in ELEMENT_KINDS:
        if re.fullmatch(re.escape(kind) + r"_\d+\w*", name):
            return kind
    return None


def position(matrix: str) -> tuple[float, float, float]:
    row = matrix.strip("()").split(";")[3].split(",")
    return float(row[0]), float(row[1]), float(row[2])


def check(level: Path, nmo: str) -> Report:
    report = Report()
    objects = nmo_json(nmo, "object", "list", str(level))["objects"]
    names_by_class = defaultdict(list)
    for obj in objects:
        names_by_class[obj["class_name"]].append(obj["name"])

    groups: dict[str, list[dict]] = {}
    for group in nmo_json(nmo, "group", "list", str(level))["groups"]:
        if group["name"] in groups:
            report.add("error", "duplicate_group", f"two groups are named {group['name']}")
        groups[group["name"]] = nmo_json(
            nmo, "group", "show", "--id", str(group["id"]), str(level))["members"]

    entities = {}
    for obj in nmo_json(nmo, "object", "export", "--class", "CK3dObject", str(level))["objects"]:
        fields = {f["name"]: f.get("value") for f in obj["fields"]}
        entities[obj["name"]] = fields
    mesh_vertices = {m["id"]: m["vertices"]
                     for m in nmo_json(nmo, "mesh", "list", str(level))["meshes"]}

    def members(name: str) -> list[str]:
        return [m["name"] for m in groups.get(name, [])]

    def visible(name: str) -> bool:
        # The CKObject flag decides (CKObject::IsVisible; libnmo shows its bit
        # as CK_OBJECT_INTERFACEOBJ), whatever the moveable flag says.
        return "CK_OBJECT_INTERFACEOBJ" in str(entities.get(name, {}).get("visibility_flags", ""))

    # What the file holds
    others = sorted({c for c in names_by_class if c not in LEVEL_CLASSES})
    if others:
        report.add("info", "extra_classes",
                   "the original levels hold only 3D objects, meshes, materials, textures "
                   f"and groups; this one also has {', '.join(others)}")

    # Start, end
    for group, prefix in (("PS_Levelstart", "PS_FourFlames"), ("PE_Levelende", "PE_Balloon")):
        held = members(group)
        if group not in groups:
            report.add("error", "missing_marker_group", f"no {group} group")
        elif len(held) != 1:
            report.add("error", "marker_count", f"{group} must hold one object, holds {len(held)}")
        elif not held[0].startswith(prefix + "_"):
            report.add("warning", "marker_name", f"{group} holds {held[0]}, expected {prefix}_01")

    # Sectors: reset points, checkpoints
    resets = members("PR_Resetpoints")
    checkpoints = members("PC_Checkpoints")
    if not resets:
        report.add("error", "no_resetpoints", "PR_Resetpoints is missing or empty: no ball spawns")
    reset_numbers = numbered(resets, "PR_Resetpoint")
    checkpoint_numbers = numbered(checkpoints, "PC_TwoFlames")
    sectors = len(resets)
    if resets and reset_numbers != list(range(1, sectors + 1)):
        report.add("error", "resetpoint_numbers",
                   f"reset points must be PR_Resetpoint_01..{sectors:02d}: {sorted(resets)}")
    if len(checkpoints) != max(sectors - 1, 0):
        report.add("error", "checkpoint_count",
                   f"{sectors} reset points need {max(sectors - 1, 0)} checkpoints, "
                   f"PC_Checkpoints holds {len(checkpoints)}")
    elif checkpoints and checkpoint_numbers != list(range(1, len(checkpoints) + 1)):
        report.add("error", "checkpoint_numbers",
                   f"checkpoints must be PC_TwoFlames_01..{len(checkpoints):02d}: {sorted(checkpoints)}")
    for n in range(1, len(checkpoints) + 1):
        pc, pr = f"PC_TwoFlames_{n:02d}", f"PR_Resetpoint_{n + 1:02d}"
        if pc in entities and pr in entities:
            (cx, cy, cz), (rx, ry, rz) = (position(entities[pc]["world_matrix"]),
                                          position(entities[pr]["world_matrix"]))
            if abs(cx - rx) > CHECKPOINT_TOLERANCE or abs(cz - rz) > CHECKPOINT_TOLERANCE:
                report.add("warning", "resetpoint_off_checkpoint",
                           f"{pr} is not at {pc}: sector {n + 1} respawns away from its checkpoint")
            elif ry <= cy:
                report.add("warning", "resetpoint_below_checkpoint",
                           f"{pr} is not above {pc} (originals: 3.33 higher)")

    # The start and the checkpoints are drawn with the level's own meshes.
    for name in members("PS_Levelstart") + checkpoints:
        mesh = entities.get(name, {}).get("current_mesh")
        if mesh_vertices.get(mesh, 0) <= PLACEHOLDER_VERTICES:
            report.add("warning", "marker_without_geometry",
                       f"{name} has a {mesh_vertices.get(mesh, 0)}-vertex mesh; the game draws the "
                       "start platform and the checkpoints with the level's mesh, so it shows as that")

    # Sector and element groups
    sector_of = defaultdict(list)
    for group, held in groups.items():
        match = re.fullmatch(r"Sector_(\d+)", group)
        if match is None:
            continue
        number = int(match.group(1))
        if sectors and number > sectors:
            report.add("warning", "sector_out_of_range",
                       f"{group} is past the last sector ({sectors})")
        for member in held:
            if element_kind(member["name"]) is None:
                report.add("warning", "sector_non_element",
                           f"{group} holds {member['name']}; sector groups hold P_* elements only")
            sector_of[member["name"]].append(group)

    elements = [n for n in names_by_class["CK3dObject"] if n.startswith("P_")]
    for name in elements:
        kind = element_kind(name)
        if kind is None:
            report.add("warning", "unknown_element", f"{name} is no element kind the game knows")
            continue
        if name not in members(kind):
            report.add("warning", "element_not_in_kind",
                       f"{name} is not in {kind}, so the game does not make it")
        if len(sector_of.get(name, [])) != 1:
            report.add("warning", "element_sector",
                       f"{name} must be in one Sector_NN group, is in "
                       f"{', '.join(sector_of.get(name, [])) or 'none'}")
    for kind in sorted(ELEMENT_KINDS):
        for member in members(kind):
            if element_kind(member) != kind:
                report.add("warning", "element_group_member", f"{kind} holds {member}")
    missing_kinds = sorted(ELEMENT_KINDS - groups.keys())
    if missing_kinds:
        report.add("info", "missing_element_groups",
                   f"{len(missing_kinds)} element groups are missing (one load-time error each, "
                   f"as in the original levels): {', '.join(missing_kinds)}")

    # Floors, sounds, shadow
    if not members("Phys_Floors"):
        report.add("error", "no_floors", "Phys_Floors is missing or empty: the ball falls through")
    sounding = set()
    for group in SOUND_GROUPS:
        sounding.update(members(group))
    # The originals leave stoppers and hidden colliders silent on purpose.
    silent = [name for group in ("Phys_Floors", "Phys_FloorRails") for name in members(group)
              if name not in sounding and visible(name)]
    if silent:
        report.add("info", "silent_floors",
                   f"{len(silent)} visible floors or rails are in no Sound_*ID group, so the "
                   f"ball makes no sound on them: {', '.join(silent)}")
    missing_named = [g for g in NAMED_GROUPS if g not in groups]
    if missing_named:
        report.add("info", "missing_groups",
                   f"no {', '.join(missing_named)} (one load-time error each)")

    # The fall zone
    if "DepthTestCubes" not in groups:
        report.add("error", "no_depth_test_cubes",
                   "no DepthTestCubes group: the depth test reports an error every frame")
    elif not members("DepthTestCubes"):
        report.add("warning", "empty_depth_test_cubes",
                   "DepthTestCubes is empty: a ball off the track falls forever")
    for name in members("DepthTestCubes"):
        if visible(name):
            report.add("warning", "visible_depth_test_cube",
                       f"{name} is visible; the original fall zones are hidden")

    # The sky
    if "SkyLayer" not in names_by_class["CK3dObject"]:
        report.add("warning", "no_sky_layer", "no SkyLayer object; every original level has one")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check a Ballance level file against the level format rules.")
    parser.add_argument("level", help="Level file (Level_NN.NMO)")
    parser.add_argument("--nmo", default=os.environ.get("NMO", "nmo"),
                        help="nmo CLI to run (default: $NMO or nmo on PATH)")
    parser.add_argument("--json", action="store_true", help="Print the findings as JSON")
    args = parser.parse_args()

    try:
        report = check(Path(args.level), args.nmo)
    except (RuntimeError, OSError, KeyError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps({"level": args.level, "findings": report.items}, indent=2))
    else:
        for item in report.items:
            print(f"{item['severity']:7s} {item['code']}: {item['message']}")
        print(f"{report.count('error')} errors, {report.count('warning')} warnings, "
              f"{report.count('info')} notes")
    return 1 if report.count("error") else 0


if __name__ == "__main__":
    sys.exit(main())
