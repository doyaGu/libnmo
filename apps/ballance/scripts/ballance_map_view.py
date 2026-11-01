#!/usr/bin/env python3
"""Draw a Ballance level from any side, with a route and a ball's path on it.

    ballance_map_view.py SOURCE -o view.png [--box X0 X1 Z0 Z1 | --around NAME [R]]
        [--views top,front,side,iso] [--yaw DEG --pitch DEG] [--scale PX]
        [--textures DIR] [--route route_points.json] [--trace steer.csv]

SOURCE is a level file (Level_NN.NMO, read through the nmo CLI) or a folder a
level was built in (manifest.json and its OBJ meshes, as
ballance_build_level.py writes them).  The objects are drawn in their world
places, orthographically, with a depth buffer and flat light, coloured by
what they are: floors (Phys_Floors) as stone, their walls darker, rails
(Phys_FloorRails) as steel, stoppers as wood, the rest grey; with --textures
(the game's Textures folder) the materials' own textures are drawn instead.
Each mechanism, element and marker (MECHANISMS: the P_* groups the game
fills from 3D Entities/PH, and the start, checkpoints, reset points and
balloon) is drawn in a colour of its own and named beside it in every view,
with its sector; a legend under the views lists those in the image, how
many, and what is known of how each plays.
Hidden objects (the fall zone, the PH placeholders the game hides) are left
out, and so is the SkyLayer.

The views, each a panel of the image:

- top: from above, X to the right and Z up;
- front: from -Z looking along +Z, X to the right and Y up;
- side: from -X looking along +X, -Z to the right and Y up;
- iso: four views 35 degrees down, from each corner;
- view: from --yaw (degrees about Y; 0 looks along +Z) and --pitch (degrees
  down).

Each panel is titled with its view and the axes that run right and up in it;
the single views share a row, and the four iso views have one of their own.

--route draws a builder's route_points.json: the path, its points numbered,
the points the ball stops at (a height to be lifted to, frames to hold)
ringed.  --trace draws the path a ball rolled, as `nmo behavior run
--steer-log` writes it (frame, x, y, z, ...), coloured from slow (blue) to
fast (red), with a frame number every 100 frames.  With a built level, its
route_points.json is drawn unless --route is given.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

PRESETS = Path(__file__).resolve().parent.parent / "data" / "level_materials.json"

# Colours (r, g, b) by what an object is.
FLOOR, WALL, RAIL, STOPPER, OTHER = (205, 190, 155), (140, 120, 95), (175, 185, 210), \
    (150, 100, 55), (130, 130, 130)
# The mechanisms, elements and markers: (group, or name prefix for the
# markers; short name; colour; how it plays, as far as known).
MECHANISMS = [
    ("P_Modul_01", "pusher", (255, 120, 60),
     "stone key in a groove under the road and a frame with a target; woken near the ball"),
    ("P_Modul_03", "gate", (190, 140, 255), "walls and a gate on a floor of its own"),
    ("P_Modul_08", "swing", (120, 220, 120), "a swing hung from a fixed frame"),
    ("P_Modul_17", "rotating arms", (255, 90, 160), "arms turning over the track"),
    ("P_Modul_18", "fan", (80, 200, 255), "lifts a paper ball 21 up"),
    ("P_Modul_19", "flaps", (230, 200, 90), "flaps on an axle"),
    ("P_Modul_25", "drawbridge", (255, 170, 60), "plank the ball pushes over a gap 10 long"),
    ("P_Modul_26", "sack", (200, 120, 70), "swings across the track"),
    ("P_Modul_29", "plates", (140, 200, 200), "nine plates"),
    ("P_Modul_30", "seesaw", (170, 230, 70), "a seesaw"),
    ("P_Modul_34", "stone on box", (180, 180, 220),
     "stands on a box in a lower channel; drops flush when the box is pushed out"),
    ("P_Modul_37", "switch", (255, 60, 60),
     "a stone ball on the target tips the plank across the cross track's gap"),
    ("P_Modul_41", "slabs", (60, 220, 200), "tip under a wood or stone ball; paper crosses"),
    ("P_Trafo_Paper", "to paper", (245, 245, 245), "ball changer"),
    ("P_Trafo_Stone", "to stone", (150, 150, 160), "ball changer"),
    ("P_Trafo_Wood", "to wood", (190, 130, 70), "ball changer"),
    ("P_Ball_Paper", "paper ball", (235, 235, 210), "loose ball to push"),
    ("P_Ball_Stone", "stone ball", (120, 120, 130), "loose ball to push"),
    ("P_Ball_Wood", "wood ball", (170, 110, 50), "loose ball to push"),
    ("P_Box", "box", (150, 95, 50), "box to push"),
    ("P_Dome", "dome", (90, 170, 90), "dome"),
    ("P_Extra_Point", "extra point", (250, 220, 40), "points"),
    ("P_Extra_Life", "extra life", (250, 120, 200), "a life"),
    ("PS_", "start", (230, 40, 40), "the level starts here"),
    ("PC_", "checkpoint", (230, 40, 40), "ends a sector"),
    ("PR_", "reset point", (240, 110, 110), "the ball comes back here"),
    ("PE_", "balloon", (230, 40, 40), "the level ends here"),
]
BACKGROUND = (24, 26, 32)
LIGHT = np.array([0.35, 0.8, -0.5]) / np.linalg.norm([0.35, 0.8, -0.5])


def nmo_json(nmo: str, *args: str) -> dict:
    out = subprocess.run([nmo, "-f", "json", *args], check=True, capture_output=True, text=True)
    return json.loads(out.stdout)["data"]


def read_obj(path: Path) -> list[tuple[str, list, list]]:
    """The OBJ's triangles: (material, three points, three uvs or None)."""
    vs, vts, tris, material = [], [], [], None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        t = line.split()
        if not t:
            continue
        if t[0] == "v":
            vs.append(tuple(float(x) for x in t[1:4]))
        elif t[0] == "vt":
            vts.append(tuple(float(x) for x in t[1:3]))
        elif t[0] == "usemtl":
            material = t[1]
        elif t[0] == "f":
            refs = []
            for w in t[1:]:
                parts = w.split("/")
                v = int(parts[0])
                vt = int(parts[1]) if len(parts) > 1 and parts[1] else None
                refs.append((vs[v - 1 if v > 0 else len(vs) + v],
                             None if vt is None else vts[vt - 1 if vt > 0 else len(vts) + vt]))
            for k in range(1, len(refs) - 1):
                corner = [refs[0], refs[k], refs[k + 1]]
                uvs = [c[1] for c in corner]
                tris.append((material, [c[0] for c in corner], None if None in uvs else uvs))
    return tris


def matrix_of(text: str) -> list[float]:
    return [float(n) for n in re.findall(r"-?[\d.]+(?:e-?\d+)?", text)]


def load_level(path: Path, nmo: str) -> list[dict]:
    """The visible 3D objects of a level file: name, groups, world triangles."""
    stat = path.stat()
    cache = Path(tempfile.gettempdir()) / "ballance_map_view" / f"{path.stem}-{int(stat.st_mtime)}"
    if not (cache / "objects.json").exists():
        (cache / "meshes").mkdir(parents=True, exist_ok=True)
        subprocess.run([nmo, "mesh", "export", "--all", "-d", str(cache / "meshes"), str(path)],
                       check=True, capture_output=True)
        objects = nmo_json(nmo, "object", "export", "--class", "CK3dObject", str(path))["objects"]
        groups: dict[str, list[str]] = {}
        for group in nmo_json(nmo, "group", "list", str(path))["groups"]:
            members = nmo_json(nmo, "group", "show", "--id", str(group["id"]), str(path))["members"]
            for member in members:
                groups.setdefault(member["name"], []).append(group["name"])
        out = []
        for obj in objects:
            fields = {f["name"]: f.get("value") for f in obj["fields"]}
            out.append({"name": obj["name"], "mesh": fields.get("current_mesh"),
                        "matrix": matrix_of(fields.get("world_matrix") or ""),
                        "visible": fields.get("visibility_flags") != "CK_OBJECT_FLAGS(0x0)",
                        "groups": groups.get(obj["name"], [])})
        (cache / "objects.json").write_text(json.dumps(out), encoding="utf-8")
    objects = json.loads((cache / "objects.json").read_text(encoding="utf-8"))
    meshes = {}
    for obj_path in (cache / "meshes").glob("*.obj"):
        found = re.search(r"_(\d+)\.obj$", obj_path.name)
        if found:
            meshes[int(found.group(1))] = obj_path
    scene = []
    for obj in objects:
        mesh = obj["mesh"]
        if not obj["visible"] or not mesh or int(mesh) not in meshes or len(obj["matrix"]) < 16:
            continue
        m = obj["matrix"]
        tris = []
        for material, points, uvs in read_obj(meshes[int(mesh)]):
            world = [(p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12],
                      p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13],
                      p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14]) for p in points]
            tris.append((material, world, uvs))
        scene.append({"name": obj["name"], "groups": obj["groups"], "tris": tris,
                      "at": (m[12], m[13], m[14])})
    return scene


def load_project(folder: Path) -> list[dict]:
    """The visible objects of a built level's manifest.json."""
    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    scene = []
    for scn in manifest.get("scenes", []):
        for obj in scn.get("objects", []):
            mesh = (obj.get("mesh") or {}).get("obj")
            if not mesh or obj.get("visible") is False or not (folder / mesh).exists():
                continue
            pos = obj.get("transform", {}).get("position", [0, 0, 0])
            turn = obj.get("transform", {}).get("rotation_euler_deg", [0, 0, 0])[1]
            c, s = math.cos(math.radians(turn)), math.sin(math.radians(turn))
            tris = []
            for material, points, uvs in read_obj(folder / mesh):
                # a turn about Y: 90 degrees takes X to -Z
                world = [(p[0] * c + p[2] * s + pos[0], p[1] + pos[1], -p[0] * s + p[2] * c + pos[2])
                         for p in points]
                tris.append((material, world, uvs))
            scene.append({"name": obj["name"], "groups": obj.get("groups", []), "tris": tris,
                          "at": tuple(pos)})
    return scene


def mechanism_of(obj: dict) -> tuple | None:
    """The MECHANISMS entry the object is, if any."""
    for entry in MECHANISMS:
        key = entry[0]
        if key.endswith("_") and obj["name"].startswith(key) or key in obj["groups"]:
            return entry
    return None


def sector_of(obj: dict) -> str:
    for group in obj["groups"]:
        if group.startswith("Sector_"):
            return "S" + group[len("Sector_"):].lstrip("0")
    return ""


def colour_of(obj: dict, material: str | None) -> tuple[int, int, int]:
    groups = obj["groups"]
    mechanism = mechanism_of(obj)
    if mechanism is not None:
        return mechanism[2]
    if "Phys_FloorRails" in groups:
        return RAIL
    if "Phys_FloorStopper" in groups:
        return STOPPER
    if "Phys_Floors" in groups:
        return WALL if material == "Floor_Side" else FLOOR
    return OTHER


class Textures:
    """The materials' textures from the game's Textures folder."""

    def __init__(self, folder: Path | None) -> None:
        self.folder = folder
        self.files = {}
        if folder is not None:
            presets = json.loads(PRESETS.read_text(encoding="utf-8"))
            self.files = {m["name"]: m.get("texture") for m in presets["materials"]}
            self.names = {p.name.lower(): p for p in folder.iterdir()}
        self.cache: dict[str, np.ndarray | None] = {}

    def get(self, material: str | None) -> np.ndarray | None:
        if self.folder is None or material is None:
            return None
        if material not in self.cache:
            name = self.files.get(material)
            path = self.names.get(name.lower()) if name else None
            self.cache[material] = (np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
                                    if path else None)
        return self.cache[material]


def basis(yaw: float, pitch: float) -> np.ndarray:
    """Rows: screen right, screen up and toward the viewer, for a view along
    yaw (degrees about Y, 0 looking along +Z) and pitch (degrees down)."""
    y, p = math.radians(yaw), math.radians(pitch)
    forward = np.array([math.sin(y) * math.cos(p), -math.sin(p), math.cos(y) * math.cos(p)])
    right = np.array([math.cos(y), 0.0, -math.sin(y)])
    up = np.cross(forward, right)
    if up[1] < 0 or (abs(up[1]) < 1e-9 and np.dot(up, [math.sin(y), 0, math.cos(y)]) < 0):
        up = -up
    return np.array([right, up, -forward])


VIEWS = {"top": [("top", 0.0, 90.0)], "front": [("front", 0.0, 0.0)],
         "side": [("side", 90.0, 0.0)],
         "iso": [(f"iso {yaw:g}", yaw, 35.0) for yaw in (45.0, 135.0, 225.0, 315.0)]}


def render(scene: list[dict], textures: Textures, yaw: float, pitch: float, scale: float,
           bounds: tuple, overlays: list) -> Image.Image:
    """One view: the scene's triangles within bounds (x0, x1, y0, y1, z0, z1),
    then the overlays, lists of (points, colour) or labels."""
    rot = basis(yaw, pitch)
    x0, x1, y0, y1, z0, z1 = bounds
    corners = np.array([[x, y, z] for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)])
    screen = corners @ rot.T
    left, right_ = screen[:, 0].min(), screen[:, 0].max()
    bottom, top = screen[:, 1].min(), screen[:, 1].max()
    w, h = max(int((right_ - left) * scale), 1), max(int((top - bottom) * scale), 1)
    image = np.full((h, w, 3), BACKGROUND, dtype=np.float32)
    depth = np.full((h, w), -np.inf)
    for obj in scene:
        for material, points, uvs in obj["tris"]:
            p = np.array(points)
            if (p[:, 0].max() < x0 or p[:, 0].min() > x1 or p[:, 2].max() < z0 or
                    p[:, 2].min() > z1 or p[:, 1].max() < y0 - 50 or p[:, 1].min() > y1 + 50):
                continue
            normal = np.cross(p[1] - p[0], p[2] - p[0])
            length = np.linalg.norm(normal)
            if length < 1e-9:
                continue
            shade = 0.55 + 0.45 * abs(float(np.dot(normal / length, LIGHT)))
            s = p @ rot.T
            sx = (s[:, 0] - left) * scale
            sy = (top - s[:, 1]) * scale
            lo_x, hi_x = int(max(math.floor(sx.min()), 0)), int(min(math.ceil(sx.max()), w))
            lo_y, hi_y = int(max(math.floor(sy.min()), 0)), int(min(math.ceil(sy.max()), h))
            if lo_x >= hi_x or lo_y >= hi_y:
                continue
            gx, gy = np.meshgrid(np.arange(lo_x, hi_x) + 0.5, np.arange(lo_y, hi_y) + 0.5)
            d = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
            if abs(d) < 1e-12:
                continue
            l1 = ((sy[1] - sy[2]) * (gx - sx[2]) + (sx[2] - sx[1]) * (gy - sy[2])) / d
            l2 = ((sy[2] - sy[0]) * (gx - sx[2]) + (sx[0] - sx[2]) * (gy - sy[2])) / d
            l3 = 1.0 - l1 - l2
            inside = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
            z = l1 * s[0, 2] + l2 * s[1, 2] + l3 * s[2, 2]
            region = depth[lo_y:hi_y, lo_x:hi_x]
            ok = inside & (z > region)
            if not ok.any():
                continue
            texture = textures.get(material) if uvs else None
            if texture is not None:
                uv = np.array(uvs)
                u = l1 * uv[0, 0] + l2 * uv[1, 0] + l3 * uv[2, 0]
                v = l1 * uv[0, 1] + l2 * uv[1, 1] + l3 * uv[2, 1]
                th, tw, _ = texture.shape
                tx = ((u % 1.0) * tw).astype(int) % tw
                ty = ((v % 1.0) * th).astype(int) % th
                colour = texture[ty[ok], tx[ok]] * shade
            else:
                colour = np.array(colour_of(obj, material), dtype=np.float32) * shade
            image[lo_y:hi_y, lo_x:hi_x][ok] = colour
            region[ok] = z[ok]
    out = Image.fromarray(np.clip(image, 0, 255).astype(np.uint8))
    draw = ImageDraw.Draw(out)

    def within(point) -> bool:
        return x0 <= point[0] <= x1 and z0 <= point[2] <= z1

    def to_px(point) -> tuple[float, float]:
        q = np.array(point, dtype=float) @ rot.T
        return (q[0] - left) * scale, (top - q[1]) * scale

    for overlay in overlays:
        kind = overlay[0]
        if kind == "line":
            _, points, colour, width = overlay
            for a, b in zip(points, points[1:]):
                if within(a) or within(b):
                    draw.line([to_px(a), to_px(b)], fill=colour, width=width)
        elif not within(overlay[1]):
            continue
        elif kind == "dot":
            _, point, colour, radius = overlay
            x, y = to_px(point)
            draw.ellipse([x - radius, y - radius, x + radius, y + radius], outline=colour,
                         width=2)
        elif kind == "text":
            _, point, colour, text = overlay
            x, y = to_px(point)
            draw.text((x + 4, y - 12), text, fill=colour)
        elif kind == "label":
            _, point, colour, text = overlay
            x, y = to_px(point)
            box = draw.textbbox((x + 6, y - 14), text)
            draw.rectangle([box[0] - 2, box[1] - 1, box[2] + 2, box[3] + 1], fill=(10, 10, 14),
                           outline=colour)
            draw.text((x + 6, y - 14), text, fill=colour)
            draw.line([(x, y), (box[0] - 2, box[3])], fill=colour, width=1)
    return out


def axes_label(yaw: float, pitch: float) -> str:
    rot = basis(yaw, pitch)

    def name(vector) -> str:
        k = int(np.argmax(np.abs(vector)))
        return ("-" if vector[k] < 0 else "+") + "XYZ"[k]

    return f"right {name(rot[0])}, up {name(rot[1])}"


def route_overlays(route: list, floor_y) -> list:
    out, points = [], []
    for i, point in enumerate(route):
        x, z = point[0], point[1]
        y = floor_y(x, z)
        points.append((x, y, z))
        stop = len(point) > 2
        out.append(("dot", (x, y, z), (255, 80, 80) if stop else (255, 230, 60), 6 if stop else 3))
        label = str(i)
        if len(point) > 3 and point[3]:
            label += f" hold {point[3]:g}"
        elif len(point) > 2:
            label += f" wait y {point[2]:g}"
        out.append(("text", (x, y, z), (255, 230, 60), label))
    return [("line", points, (255, 230, 60), 2)] + out


def trace_overlays(rows: list[dict]) -> list:
    out = []
    speeds = [float(r.get("speed", 0) or 0) for r in rows]
    top = max(max(speeds, default=0.0), 1e-6)
    for a, b, speed in zip(rows, rows[1:], speeds):
        t = min(speed / top, 1.0)
        colour = (int(60 + 195 * t), int(90 * (1 - t) + 40), int(255 * (1 - t)))
        out.append(("line", [(float(a["x"]), float(a["y"]), float(a["z"])),
                             (float(b["x"]), float(b["y"]), float(b["z"]))], colour, 3))
    for row in rows:
        if int(row["frame"]) % 100 == 0:
            p = (float(row["x"]), float(row["y"]), float(row["z"]))
            out.append(("dot", p, (255, 255, 255), 2))
            out.append(("text", p, (200, 220, 255), row["frame"]))
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="Draw a Ballance level from any side.")
    parser.add_argument("source", help="Level_NN.NMO, or a folder with manifest.json")
    parser.add_argument("-o", "--output", required=True, help="PNG to write")
    parser.add_argument("--box", nargs=4, type=float, metavar=("X0", "X1", "Z0", "Z1"))
    parser.add_argument("--around", nargs="+", metavar=("NAME", "R"),
                        help="Centre on an object, R (default 30) each way")
    parser.add_argument("--heights", nargs=2, type=float, metavar=("Y0", "Y1"),
                        help="Height range (default: the floors in the box)")
    parser.add_argument("--views", default="top,front,side,iso",
                        help="Comma-separated: top, front, side, iso, view")
    parser.add_argument("--yaw", type=float, default=30.0, help="For 'view': degrees about Y")
    parser.add_argument("--pitch", type=float, default=40.0, help="For 'view': degrees down")
    parser.add_argument("--scale", type=float, default=8.0, help="Pixels a unit")
    parser.add_argument("--textures", help="The game's Textures folder, to draw textures")
    parser.add_argument("--route", help="route_points.json to draw")
    parser.add_argument("--trace", help="A --steer-log CSV to draw")
    parser.add_argument("--nmo", default=os.environ.get("NMO", "nmo"),
                        help="nmo CLI to run (default: $NMO or nmo on PATH)")
    args = parser.parse_args()

    source = Path(args.source)
    scene = load_project(source) if source.is_dir() else load_level(source, args.nmo)
    scene = [obj for obj in scene if obj["name"] != "SkyLayer"]
    route_path = args.route or (str(source / "route_points.json")
                                if source.is_dir() and (source / "route_points.json").exists()
                                else None)
    route = json.loads(Path(route_path).read_text(encoding="utf-8")) if route_path else []
    trace = []
    if args.trace:
        with open(args.trace, newline="", encoding="utf-8") as f:
            trace = list(csv.DictReader(f))

    floors = [p for obj in scene if "Phys_Floors" in obj["groups"]
              for _, points, _ in obj["tris"] for p in points]
    if args.box:
        x0, x1, z0, z1 = args.box
    elif args.around:
        found = [obj for obj in scene if obj["name"] == args.around[0]]
        if not found:
            print(f"no object {args.around[0]}", file=sys.stderr)
            return 2
        r = float(args.around[1]) if len(args.around) > 1 else 30.0
        cx, _, cz = found[0]["at"]
        x0, x1, z0, z1 = cx - r, cx + r, cz - r, cz + r
    else:
        pts = floors + [(p[0], 0, p[1]) for p in route]
        x0, x1 = min(p[0] for p in pts) - 5, max(p[0] for p in pts) + 5
        z0, z1 = min(p[2] for p in pts) - 5, max(p[2] for p in pts) + 5
    inside = [p for p in floors if x0 <= p[0] <= x1 and z0 <= p[2] <= z1]
    if args.heights:
        y0, y1 = args.heights
    else:
        ys = [p[1] for p in inside] or [0.0]
        y0, y1 = min(ys) - 8, max(ys) + 12
    bounds = (x0, x1, y0, y1, z0, z1)

    def floor_y(x: float, z: float) -> float:
        near = [p[1] for p in inside if abs(p[0] - x) < 3 and abs(p[2] - z) < 3]
        return (max(near) if near else (y0 + y1) / 2) + 0.3

    # the mechanisms in the box, named over their tops
    labels, present = [], {}
    for obj in scene:
        mechanism = mechanism_of(obj)
        if mechanism is None or not obj["tris"]:
            continue
        pts = np.array([p for _, points, _ in obj["tris"] for p in points])
        cx, cz = float(pts[:, 0].mean()), float(pts[:, 2].mean())
        if not (x0 <= cx <= x1 and z0 <= cz <= z1):
            continue
        sector = sector_of(obj)
        labels.append(("label", (cx, float(pts[:, 1].max()) + 0.5, cz), mechanism[2],
                       mechanism[1] + (f" {sector}" if sector else "")))
        present[mechanism] = present.get(mechanism, 0) + 1
    overlays = route_overlays(route, floor_y) + trace_overlays(trace) + labels
    textures = Textures(Path(args.textures) if args.textures else None)
    rows = []
    for view in args.views.split(","):
        view = view.strip()
        presets = [(f"view {args.yaw:g}/{args.pitch:g}", args.yaw, args.pitch)] \
            if view == "view" else VIEWS.get(view)
        if presets is None:
            print(f"unknown view {view}", file=sys.stderr)
            return 2
        panels = [(f"{title}  ({axes_label(yaw, pitch)})",
                   render(scene, textures, yaw, pitch, args.scale, bounds, overlays))
                  for title, yaw, pitch in presets]
        if len(panels) == 1 and rows and rows[-1][0][2]:
            rows[-1].append(panels[0] + (True,))
        else:
            rows.append([panel + (len(panels) == 1,) for panel in panels])

    gap, label = 8, 18
    legend = [(entry, count) for entry, count in present.items()]
    legend.sort(key=lambda item: MECHANISMS.index(item[0]))
    legend_h = (len(legend) * 16 + gap * 2) if legend else 0
    width = max(sum(p.width for _, p, _ in row) + gap * (len(row) + 1) for row in rows)
    height = sum(max(p.height for _, p, _ in row) + label + gap for row in rows) + gap + legend_h
    width = max(width, 560 if legend else 0)
    sheet = Image.new("RGB", (width, height), (12, 12, 16))
    draw = ImageDraw.Draw(sheet)
    y = gap
    for row in rows:
        x = gap
        for title, image, _ in row:
            draw.text((x, y), title, fill=(220, 220, 220))
            sheet.paste(image, (x, y + label))
            x += image.width + gap
        y += max(p.height for _, p, _ in row) + label + gap
    for (key, name, colour, note), count in legend:
        draw.rectangle([gap, y + 2, gap + 10, y + 12], fill=colour)
        draw.text((gap + 16, y), f"{name} ({key.rstrip('_')}) x{count}: {note}",
                  fill=(220, 220, 220))
        y += 16
    sheet.save(args.output)
    print(args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
