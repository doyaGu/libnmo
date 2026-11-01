"""The nmo CLI: running it, reading what it exports, and placing exported
meshes in the world."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from pathlib import Path

Matrix = list[float]  # 16 values, row by row; rows 0-2 the axes, row 3 the position


class Nmo:
    """One nmo executable, run on files."""

    def __init__(self, executable: str | None = None) -> None:
        self.executable = executable or os.environ.get("NMO", "nmo")

    def run(self, *args: str) -> str:
        result = subprocess.run([self.executable, *args], capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError(f"nmo {' '.join(args[:2])} failed: {result.stderr.strip()}")
        return result.stdout

    def data(self, *args: str) -> dict:
        """The "data" of a command run with -f json."""
        return json.loads(self.run("-f", "json", *args))["data"]

    def objects(self, path: Path, class_name: str) -> list[dict]:
        """object export --class: each object's id, name and fields."""
        return self.data("object", "export", "--class", class_name, str(path))["objects"]

    def group_list(self, path: Path) -> list[dict]:
        return self.data("group", "list", str(path))["groups"]

    def group_members(self, path: Path, group_id: int) -> list[dict]:
        return self.data("group", "show", "--id", str(group_id), str(path))["members"]

    def groups(self, path: Path) -> dict[str, list[dict]]:
        """Each group's members, by group name."""
        return {group["name"]: self.group_members(path, group["id"])
                for group in self.group_list(path)}

    def export_mesh(self, path: Path, mesh_id: int, folder: Path) -> None:
        self.run("mesh", "export", "--id", str(mesh_id), "-d", str(folder), str(path))

    def export_meshes(self, path: Path, folder: Path) -> None:
        self.run("mesh", "export", "--all", "-d", str(folder), str(path))


def add_nmo_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--nmo", default=os.environ.get("NMO", "nmo"),
                        help="nmo CLI to run (default: $NMO or nmo on PATH)")


def field_values(obj: dict) -> dict:
    """An exported object's fields, name to value."""
    return {f["name"]: f.get("value") for f in obj["fields"]}


def matrix_of(text: str) -> Matrix:
    """The numbers of an exported matrix, "(a, b, c, d; ...)"."""
    return [float(n) for n in re.findall(r"-?[\d.]+(?:e-?\d+)?", text or "")]


def place(m: Matrix, p: tuple[float, float, float]) -> tuple[float, float, float]:
    """A point of an object's mesh where the object's world matrix puts it."""
    return tuple(p[0] * m[i] + p[1] * m[4 + i] + p[2] * m[8 + i] + m[12 + i] for i in range(3))


def turn(m: Matrix, n: tuple[float, float, float]) -> tuple[float, float, float]:
    """A direction of an object's mesh (a normal) turned as its matrix turns it."""
    return tuple(n[0] * m[i] + n[1] * m[4 + i] + n[2] * m[8 + i] for i in range(3))


def into(m: Matrix, p: tuple[float, float, float], w: float = 1.0) -> tuple[float, float, float]:
    """A world point (w 1) or direction (w 0) in the frame of an object placed
    by m, unscaled."""
    d = [p[i] - m[12 + i] * w for i in range(3)]
    return tuple(sum(d[k] * m[4 * i + k] for k in range(3)) for i in range(3))
