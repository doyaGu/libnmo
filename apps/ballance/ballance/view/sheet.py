"""A sheet of a level's views: the part of the level to draw (Frame), the
views (VIEWS) as panels, the single views sharing a row and the four iso
views a row of their own, and a legend of the mechanisms in it."""

from __future__ import annotations

from dataclasses import dataclass

from PIL import Image, ImageDraw

from ..game.catalog import MECHANISMS, Mechanism
from .render import Camera, Textures, render
from .scene import SceneObject

# Each view: (title, yaw, pitch); "view" is the one --yaw and --pitch give.
VIEWS = {"top": [("top", 0.0, 90.0)], "front": [("front", 0.0, 0.0)],
         "side": [("side", 90.0, 0.0)],
         "iso": [(f"iso {yaw:g}", yaw, 35.0) for yaw in (45.0, 135.0, 225.0, 315.0)]}

GAP, TITLE, LEGEND_LINE = 8, 18, 16
SHEET, TITLE_TEXT = (12, 12, 16), (220, 220, 220)


@dataclass
class Frame:
    """The part of the level drawn: X and Z, the heights, and the floors'
    points within it."""
    x0: float
    x1: float
    z0: float
    z1: float
    y0: float
    y1: float
    floors: list

    @property
    def bounds(self) -> tuple:
        return self.x0, self.x1, self.y0, self.y1, self.z0, self.z1

    @property
    def box(self) -> tuple:
        return self.x0, self.x1, self.z0, self.z1

    def floor_y(self, x: float, z: float) -> float:
        """Just above the highest floor near (x, z), or the middle height."""
        near = [p[1] for p in self.floors if abs(p[0] - x) < 3 and abs(p[2] - z) < 3]
        return (max(near) if near else (self.y0 + self.y1) / 2) + 0.3


def frame(scene: list[SceneObject], route: list, box: tuple | None,
          heights: tuple | None) -> Frame:
    """The frame of box (x0, x1, z0, z1), or of all the floors and the
    route; its heights those given, or the floors' in it with room above and
    below."""
    floors = [p for obj in scene if "Phys_Floors" in obj.groups
              for t in obj.triangles for p in t.points]
    if box is not None:
        x0, x1, z0, z1 = box
    else:
        pts = floors + [(p[0], 0, p[1]) for p in route]
        x0, x1 = min(p[0] for p in pts) - 5, max(p[0] for p in pts) + 5
        z0, z1 = min(p[2] for p in pts) - 5, max(p[2] for p in pts) + 5
    inside = [p for p in floors if x0 <= p[0] <= x1 and z0 <= p[2] <= z1]
    if heights is not None:
        y0, y1 = heights
    else:
        ys = [p[1] for p in inside] or [0.0]
        y0, y1 = min(ys) - 8, max(ys) + 12
    return Frame(x0, x1, z0, z1, y0, y1, inside)


Panel = tuple[str, Image.Image]


def panels(scene: list[SceneObject], textures: Textures, frame_: Frame, views: list[str],
           yaw: float, pitch: float, scale: float, overlays: list) -> list[list[Panel]]:
    """The views drawn, in rows: the single views share one, each set of
    several (iso) has its own."""
    rows: list[list[tuple[str, Image.Image, bool]]] = []
    for view in views:
        presets = [(f"view {yaw:g}/{pitch:g}", yaw, pitch)] if view == "view" else VIEWS.get(view)
        if presets is None:
            raise ValueError(f"unknown view {view}")
        drawn = []
        for title, view_yaw, view_pitch in presets:
            camera = Camera(view_yaw, view_pitch, frame_.bounds, scale)
            image = render(scene, textures, camera)
            draw = ImageDraw.Draw(image)
            for overlay in overlays:
                overlay.draw(draw, camera)
            drawn.append((f"{title}  ({camera.axes()})", image))
        if len(drawn) == 1 and rows and rows[-1][0][2]:
            rows[-1].append(drawn[0] + (True,))
        else:
            rows.append([panel + (len(drawn) == 1,) for panel in drawn])
    return [[(title, image) for title, image, _ in row] for row in rows]


def sheet(rows: list[list[Panel]], present: dict[Mechanism, int]) -> Image.Image:
    """The panels with their titles, and the legend under them."""
    legend = sorted(present.items(), key=lambda item: MECHANISMS.index(item[0]))
    legend_h = (len(legend) * LEGEND_LINE + GAP * 2) if legend else 0
    width = max(sum(p.width for _, p in row) + GAP * (len(row) + 1) for row in rows)
    height = sum(max(p.height for _, p in row) + TITLE + GAP for row in rows) + GAP + legend_h
    width = max(width, 560 if legend else 0)
    out = Image.new("RGB", (width, height), SHEET)
    draw = ImageDraw.Draw(out)
    y = GAP
    for row in rows:
        x = GAP
        for title, image in row:
            draw.text((x, y), title, fill=TITLE_TEXT)
            out.paste(image, (x, y + TITLE))
            x += image.width + GAP
        y += max(p.height for _, p in row) + TITLE + GAP
    for mechanism, count in legend:
        draw.rectangle([GAP, y + 2, GAP + 10, y + 12], fill=mechanism.colour)
        draw.text((GAP + 16, y), f"{mechanism.name} ({mechanism.key.rstrip('_')}) "
                                 f"x{count}: {mechanism.note}", fill=TITLE_TEXT)
        y += LEGEND_LINE
    return out


def around(scene: list[SceneObject], name: str, reach: float) -> tuple | None:
    """The box reach each way around an object, or None if there is none."""
    found = [obj for obj in scene if obj.name == name]
    if not found:
        return None
    cx, _, cz = found[0].at
    return cx - reach, cx + reach, cz - reach, cz + reach
