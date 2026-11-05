"""Drawing a scene orthographically from a direction, with a depth buffer
and a fixed light.

Objects are coloured by what they are (colour_of): floors (Phys_Floors) as
stone, their walls darker, rails as steel, stoppers as wood, each mechanism,
element and marker in its catalog colour, the rest grey; with the game's
textures (Textures), the materials' own textures are drawn instead, except
on rails: the game maps their environment texture at run time, so their UVs
follow no rule, and they are drawn as polished steel, shaded smoothly by
their normals with a highlight.
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np
from PIL import Image

from ..game.catalog import mechanism_of
from ..game.materials import textures_by_material
from .scene import SceneObject

FLOOR, WALL, RAIL, STOPPER, OTHER = (
    (205, 190, 155),
    (140, 120, 95),
    (175, 185, 210),
    (150, 100, 55),
    (130, 130, 130),
)
BACKGROUND = (24, 26, 32)
LIGHT = np.array([0.35, 0.8, -0.5]) / np.linalg.norm([0.35, 0.8, -0.5])

Bounds = tuple[float, float, float, float, float, float]  # x0, x1, y0, y1, z0, z1


def colour_of(obj: SceneObject, material: str | None) -> tuple[int, int, int]:
    mechanism = mechanism_of(obj.name, obj.groups)
    if mechanism is not None:
        return mechanism.colour
    if 'Phys_FloorRails' in obj.groups:
        return RAIL
    if 'Phys_FloorStopper' in obj.groups:
        return STOPPER
    if 'Phys_Floors' in obj.groups:
        return WALL if material == 'Floor_Side' else FLOOR
    return OTHER


def is_rail(obj: SceneObject, material: str | None) -> bool:
    return 'Phys_FloorRails' in obj.groups or (material or '').startswith('Rail')


class Textures:
    """The materials' textures, from the game's Textures folder (or none)."""

    def __init__(self, folder: Path | None) -> None:
        self.folder = folder
        self.files: dict[str, str | None] = {}
        if folder is not None:
            self.files = textures_by_material()
            self.names = {p.name.lower(): p for p in folder.iterdir()}
        self.cache: dict[str, np.ndarray | None] = {}

    def get(self, material: str | None) -> np.ndarray | None:
        if self.folder is None or material is None:
            return None
        if material not in self.cache:
            name = self.files.get(material)
            path = self.names.get(name.lower()) if name else None
            self.cache[material] = (
                np.asarray(Image.open(path).convert('RGB'), dtype=np.float32) if path else None
            )
        return self.cache[material]


class Camera:
    """An orthographic view along yaw (degrees about Y, 0 looking along +Z)
    and pitch (degrees down), framing bounds at scale pixels a unit."""

    def __init__(self, yaw: float, pitch: float, bounds: Bounds, scale: float) -> None:
        y, p = math.radians(yaw), math.radians(pitch)
        forward = np.array([math.sin(y) * math.cos(p), -math.sin(p), math.cos(y) * math.cos(p)])
        right = np.array([math.cos(y), 0.0, -math.sin(y)])
        up = np.cross(forward, right)
        if up[1] < 0 or (abs(up[1]) < 1e-9 and np.dot(up, [math.sin(y), 0, math.cos(y)]) < 0):
            up = -up
        # rows: screen right, screen up, toward the viewer
        self.rot = np.array([right, up, -forward])
        self.bounds, self.scale = bounds, scale
        x0, x1, y0, y1, z0, z1 = bounds
        corners = np.array([[x, y, z] for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)])
        screen = corners @ self.rot.T
        self.left, right_ = screen[:, 0].min(), screen[:, 0].max()
        bottom, self.top = screen[:, 1].min(), screen[:, 1].max()
        self.width = max(int((right_ - self.left) * scale), 1)
        self.height = max(int((self.top - bottom) * scale), 1)

    def axes(self) -> str:
        """Which axes run right and up, as "right +X, up +Z"."""

        def name(vector) -> str:
            k = int(np.argmax(np.abs(vector)))
            return ('-' if vector[k] < 0 else '+') + 'XYZ'[k]

        return f'right {name(self.rot[0])}, up {name(self.rot[1])}'

    def pixel(self, point) -> tuple[float, float]:
        q = np.array(point, dtype=float) @ self.rot.T
        return (q[0] - self.left) * self.scale, (self.top - q[1]) * self.scale

    def within(self, point) -> bool:
        """Whether a point lies in the bounds, on X and Z."""
        x0, x1, _, _, z0, z1 = self.bounds
        return x0 <= point[0] <= x1 and z0 <= point[2] <= z1


def render(scene: list[SceneObject], textures: Textures, camera: Camera) -> Image.Image:
    """The scene's triangles within the camera's bounds, nearest in front."""
    image = np.full((camera.height, camera.width, 3), BACKGROUND, dtype=np.float32)
    depth = np.full((camera.height, camera.width), -np.inf)
    for obj in scene:
        for triangle in obj.triangles:
            _draw_triangle(image, depth, camera, textures, obj, triangle)
    return Image.fromarray(np.clip(image, 0, 255).astype(np.uint8))


def _culled(p: np.ndarray, bounds: Bounds) -> bool:
    x0, x1, y0, y1, z0, z1 = bounds
    return (
        p[:, 0].max() < x0
        or p[:, 0].min() > x1
        or p[:, 2].max() < z0
        or p[:, 2].min() > z1
        or p[:, 1].max() < y0 - 50
        or p[:, 1].min() > y1 + 50
    )


def _draw_triangle(
    image: np.ndarray,
    depth: np.ndarray,
    camera: Camera,
    textures: Textures,
    obj: SceneObject,
    triangle,
) -> None:
    material, points, uvs, normals = triangle
    p = np.array(points)
    if _culled(p, camera.bounds):
        return
    normal = np.cross(p[1] - p[0], p[2] - p[0])
    length = np.linalg.norm(normal)
    if length < 1e-9:
        return
    shade = 0.55 + 0.45 * abs(float(np.dot(normal / length, LIGHT)))
    # smooth surfaces (rails) shade by their vertex normals, across the face
    smooth = normals is not None and any(
        np.dot(np.array(n), normal) < 0.999 * np.linalg.norm(n) * length for n in normals
    )
    # where on screen, and which pixels it covers
    w, h, scale = camera.width, camera.height, camera.scale
    s = p @ camera.rot.T
    sx = (s[:, 0] - camera.left) * scale
    sy = (camera.top - s[:, 1]) * scale
    lo_x, hi_x = int(max(math.floor(sx.min()), 0)), int(min(math.ceil(sx.max()), w))
    lo_y, hi_y = int(max(math.floor(sy.min()), 0)), int(min(math.ceil(sy.max()), h))
    if lo_x >= hi_x or lo_y >= hi_y:
        return
    gx, gy = np.meshgrid(np.arange(lo_x, hi_x) + 0.5, np.arange(lo_y, hi_y) + 0.5)
    d = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
    if abs(d) < 1e-12:
        return
    l1 = ((sy[1] - sy[2]) * (gx - sx[2]) + (sx[2] - sx[1]) * (gy - sy[2])) / d
    l2 = ((sy[2] - sy[0]) * (gx - sx[2]) + (sx[0] - sx[2]) * (gy - sy[2])) / d
    l3 = 1.0 - l1 - l2
    inside = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
    z = l1 * s[0, 2] + l2 * s[1, 2] + l3 * s[2, 2]
    region = depth[lo_y:hi_y, lo_x:hi_x]
    ok = inside & (z > region)
    if not ok.any():
        return

    def blend(values) -> np.ndarray:
        """Per-corner values across the covered pixels."""
        v = np.array(values, dtype=float)
        return np.stack(
            [l1 * v[0, k] + l2 * v[1, k] + l3 * v[2, k] for k in range(v.shape[1])], axis=-1
        )

    if smooth:
        n = blend(normals)
        nx, ny, nz = n[..., 0], n[..., 1], n[..., 2]
        lit = np.abs(nx * LIGHT[0] + ny * LIGHT[1] + nz * LIGHT[2]) / np.maximum(
            np.sqrt(nx * nx + ny * ny + nz * nz), 1e-9
        )
        shade = (0.55 + 0.45 * lit[ok])[:, None]
    texture = None if is_rail(obj, material) or not uvs else textures.get(material)
    if is_rail(obj, material):
        # polished steel: diffuse plus a highlight toward the viewer
        if smooth:
            n = blend(normals)[ok]
        else:
            n = np.repeat((normal / length)[None, :], int(ok.sum()), axis=0)
        n = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
        half = LIGHT + camera.rot[2]
        half = half / np.linalg.norm(half)
        highlight = np.abs(n @ half) ** 24
        colour = (
            np.array(RAIL, dtype=np.float32) * (0.45 + 0.55 * np.abs(n @ LIGHT))[:, None]
            + 220.0 * highlight[:, None]
        )
    elif texture is not None:
        uv = blend(uvs)
        th, tw, _ = texture.shape
        tx = ((uv[..., 0] % 1.0) * tw).astype(int) % tw
        ty = ((uv[..., 1] % 1.0) * th).astype(int) % th
        colour = texture[ty[ok], tx[ok]] * shade
    else:
        colour = np.array(colour_of(obj, material), dtype=np.float32) * shade
    image[lo_y:hi_y, lo_x:hi_x][ok] = colour
    region[ok] = z[ok]
