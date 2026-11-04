"""The fall zones under a level: hidden boxes that make a ball touching one
fall off ("Ball Off"), each its own object, as the originals have a few
under their sections."""

from __future__ import annotations

import math
import json
import re
from pathlib import Path

from ..core.obj import Box, write_box, read_obj
from ..core.path import number
from ..game.measures import FALL_BOTTOM, FALL_MARGIN, FALL_STEP, FALL_TILE, FALL_TOP

Point3 = tuple[float, float, float]


def floor_points(objects: list[dict], out: Path) -> list[Point3]:
    """The corners of the manifest's floors and rails, placed in the world."""
    points = []
    for obj in objects:
        if not any(group.startswith("Phys_") for group in obj["groups"]):
            continue
        px, py, pz = obj["transform"]["position"]
        t = math.radians(obj["transform"].get("rotation_euler_deg", [0, 0, 0])[1])
        c, s = math.cos(t), math.sin(t)
        for line in (out / obj["mesh"]["obj"]).read_text(encoding="utf-8").splitlines():
            if line.startswith("v "):
                x, y, z = (float(v) for v in line.split()[1:4])
                points.append((px + x * c + z * s, py + y, pz - x * s + z * c))
    return points


def fall_boxes(points: list[Point3], tile_size=FALL_TILE, clearance=FALL_TOP,
               depth=FALL_BOTTOM-FALL_TOP, quantum=FALL_STEP, margin=FALL_MARGIN) -> list[Box]:
    """Boxes FALL_TOP to FALL_BOTTOM below the lowest floor of each FALL_TILE
    square that has floors, and of the squares round those (as deep as the
    deepest of their neighbours), the squares at one depth joined; and one
    under all of them, FALL_MARGIN beyond, for a ball thrown further."""
    lows: dict[tuple[int, int], float] = {}
    for x, y, z in points:
        tile = (math.floor(x / tile_size), math.floor(z / tile_size))
        lows[tile] = min(lows.get(tile, math.inf), y)
    tops = {tile: math.floor((low - clearance) / quantum) * quantum
            for tile, low in lows.items()}
    for (i, j), top in list(tops.items()):
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                near = (i + di, j + dj)
                if near not in lows:
                    tops[near] = min(tops.get(near, math.inf), top)
    # runs along X of squares at one depth, then runs of those along Z
    runs: dict[tuple[int, int, float], list[int]] = {}
    for j in sorted({j for _, j in tops}):
        row = sorted(i for i, jj in tops if jj == j)
        start = None
        for k, i in enumerate(row):
            if start is None:
                start = i
            ends = (k + 1 == len(row) or row[k + 1] != i + 1 or
                    tops[(row[k + 1], j)] != tops[(i, j)])
            if ends:
                runs.setdefault((start, i, tops[(i, j)]), []).append(j)
                start = None
    boxes = []
    for (i0, i1, top), rows in sorted(runs.items()):
        rows.sort()
        first = rows[0]
        for k, j in enumerate(rows):
            if k + 1 == len(rows) or rows[k + 1] != j + 1:
                boxes.append((i0 * tile_size, (i1 + 1) * tile_size, top - depth, top,
                              first * tile_size, (j + 1) * tile_size))
                if k + 1 < len(rows):
                    first = rows[k + 1]
    x0 = min(b[0] for b in boxes) - margin
    x1 = max(b[1] for b in boxes) + margin
    z0 = min(b[4] for b in boxes) - margin
    z1 = max(b[5] for b in boxes) + margin
    low = min(b[2] for b in boxes)
    boxes.append((x0, x1, low - depth, low, z0, z1))
    return boxes


def surface_cells(objects, out, tile):
    """Conservatively cover entire triangle footprints, not only vertices.
    This catches the middle of long rails and excludes decorative towers.
    Depth testing uses boxes, including the rotating ball's box. Four units
    of horizontal padding cover a radius-two ball's worst rotated AABB.
    """
    lows = {}
    for obj in objects:
        if not any(g in obj['groups'] for g in ('Phys_Floors', 'Phys_FloorRails', 'Phys_FloorStopper')):
            continue
        if re.search(r'(?:^|_)(?:Tower|Floor_Columns_Top)(?:_?\d+)?$', obj['name']):
            continue
        px,py,pz = obj['transform']['position']
        angle = math.radians(obj['transform'].get('rotation_euler_deg',[0,0,0])[1])
        c,s = math.cos(angle),math.sin(angle)
        for tri in read_obj(out / obj['mesh']['obj']):
            pts=[(px+x*c+z*s,py+y,pz-x*s+z*c) for x,y,z in tri.points]
            x0,x1=min(p[0] for p in pts)-4,max(p[0] for p in pts)+4
            z0,z1=min(p[2] for p in pts)-4,max(p[2] for p in pts)+4
            low=min(p[1] for p in pts)
            for i in range(math.floor(x0/tile),math.floor(x1/tile)+1):
                for j in range(math.floor(z0/tile),math.floor(z1/tile)+1):
                    lows[i,j]=min(lows.get((i,j),math.inf),low)
            if len(lows)>200000:
                raise ValueError('death-zone footprint exceeds 200000 cells')
    return [((i+.5)*tile,y,(j+.5)*tile) for (i,j),y in lows.items()]


def subtract(box, keep):
    """Subtract an authored recoverable volume from a death box."""
    overlap=tuple(v for k in range(0,6,2) for v in (max(box[k],keep[k]),min(box[k+1],keep[k+1])))
    if any(overlap[k]>=overlap[k+1] for k in (0,2,4)):
        return [box]
    result=[]
    core=list(box)
    for k in (0,2,4):
        if core[k]<overlap[k]:
            slab=core.copy();slab[k+1]=overlap[k];result.append(tuple(slab))
        if overlap[k+1]<core[k+1]:
            slab=core.copy();slab[k]=overlap[k+1];result.append(tuple(slab))
        core[k:k+2]=overlap[k:k+2]
    return result


def write_fall_zones(objects: list[dict], out: Path, policy=None) -> list[str]:
    """Default legacy zones, or surface-following zones with recovery volumes.
    The audit lists exact invisible boxes in [x0,x1,y0,y1,z0,z1] order.
    """
    if policy is None:
        boxes=fall_boxes(floor_points(objects,out))
    else:
        if not isinstance(policy,dict) or set(policy)-{'tile','clearance','depth','quantum','margin','keepouts'}:
            raise ValueError('unknown death_zones policy options')
        defaults={'tile':25,'clearance':18,'depth':35,'quantum':5,'margin':60}
        opts={k:number(policy.get(k,v),f'death zone {k}') for k,v in defaults.items()}
        if not 10<=opts['tile']<=200 or not 8<=opts['clearance']<=100 or not 5<=opts['depth']<=100 or not 1<=opts['quantum']<=30 or not 10<=opts['margin']<=200:
            raise ValueError('death-zone dimensions outside supported bounds')
        boxes=fall_boxes(surface_cells(objects,out,opts['tile']), opts['tile'],opts['clearance'],opts['depth'],opts['quantum'],opts['margin'])
        for entry in policy.get('keepouts',[]):
            if not isinstance(entry,dict) or not entry.get('reason') or set(entry)-{'box','reason'}:
                raise ValueError('a death-zone keepout needs box and reason')
            if not isinstance(entry.get('box'),list) or len(entry['box'])!=6:
                raise ValueError('keepout box needs x0,x1,y0,y1,z0,z1')
            keep=tuple(number(v,'keepout') for v in entry['box'])
            if any(keep[k]>=keep[k+1] for k in (0,2,4)):
                raise ValueError('keepout bounds must increase')
            boxes=[part for box in boxes for part in subtract(box,keep)]
    meshes=[]
    for index,box in enumerate(boxes,start=1):
        meshes.append(f'depth{index}.obj')
        write_box(out/meshes[-1],box)
    (out/'death_zones.json').write_text(json.dumps({
        'policy': 'legacy' if policy is None else policy, 'boxes': boxes}, indent=2)+'\n')
    return meshes
