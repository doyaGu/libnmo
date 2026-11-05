"""A path for the ball over a terrain, by A*.

A node is a layer of a cell.  From one, the ball rolls to a layer of a
neighbouring cell within CLIMB of its height, takes a step up to STEEP at a
high cost, or falls up to DROP; falling, it is carried over empty cells to
the first that has a layer.  It doesn't fall through what it rolls on, and
off a rail only over its end, leaving the rail behind it.  A step costs its
length, more the further it falls and much more the higher it climbs, and
more again the less clear the ground around it is (CLEAR) and through a box
it would push.
"""

from __future__ import annotations

import heapq
import math
from collections.abc import Iterator

from .terrain import CELL, CLIMB, STEEP, Point3, Terrain, centre

DROP = 16.0  # the furthest it may fall
CARRY = 5  # the most cells a fall carries it over empty ones
FALL_COST = 4.0  # a unit fallen past CLIMB
LONG_FALL = 3.0  # past this, a fall costs LONG_FALL_COST more
LONG_FALL_COST = 20.0
STEEP_COST = 150.0  # a unit climbed past CLIMB
CLEAR = 2.2  # how far round a cell the ground is wanted level with it
NARROW_COST = 3.0  # the most a cell costs more for having no ground round it
LEVEL = 1.2  # ground within this of a cell's height counts as round it
BOX_COST = 30.0  # a cell more, through a box

Node = tuple[int, int, int]  # cell and layer


def neighbours(ground: Terrain, node: Node) -> Iterator[tuple[Node, float]]:
    """The nodes the ball can get to from node, and what each step costs."""
    layers = ground.layers
    i, j, k = node
    h = layers[(i, j)][k]
    rail = ground.tangents.get((i, j))
    for di in (-1, 0, 1):
        for dj in (-1, 0, 1):
            if not di and not dj:
                continue
            carry = CARRY if not layers.get((i + di, j + dj)) else 1
            for n in range(1, carry + 1):
                there = layers.get((i + n * di, j + n * dj), [])
                level = any(abs(hh - h) <= CLIMB for hh in there)
                for kk, hh in enumerate(there):
                    if not -DROP <= hh - h <= (CLIMB if n > 1 else STEEP):
                        continue
                    climb = max(0.0, hh - h - CLIMB)
                    fall = max(0.0, h - hh - CLIMB)
                    if n > 1 and fall <= 0:
                        continue  # only falling carries it over empty cells
                    if fall > 0 and level:
                        continue  # it doesn't fall through what it rolls on
                    if (
                        fall > 0
                        and rail is not None
                        and (
                            abs(rail[0] * di + rail[1] * dj) < 0.95 * math.hypot(di, dj)
                            or not any(
                                abs(hb - h) <= CLIMB for hb in layers.get((i - di, j - dj), [])
                            )
                        )
                    ):
                        continue  # off a rail only over its end, leaving the rail behind
                    yield (
                        (i + n * di, j + n * dj, kk),
                        n * math.hypot(di, dj) * CELL
                        + FALL_COST * fall
                        + (LONG_FALL_COST if fall > LONG_FALL else 0.0)
                        + STEEP_COST * climb,
                    )
                if there:
                    break


def clearance(ground: Terrain) -> dict[Node, float]:
    """How much of the ground within CLEAR of each node is level with it,
    from 0 (none) to 1 (all)."""
    r = int(math.ceil(CLEAR / CELL))
    disc = [
        (di, dj) for di in range(-r, r + 1) for dj in range(-r, r + 1) if di * di + dj * dj <= r * r
    ]
    clear = {}
    for (i, j), hs in ground.layers.items():
        for k, h in enumerate(hs):
            near = sum(
                1
                for di, dj in disc
                if any(abs(hh - h) < LEVEL for hh in ground.layers.get((i + di, j + dj), []))
            )
            clear[(i, j, k)] = near / len(disc)
    return clear


def nearest(ground: Terrain, p: Point3, reach: int = 4) -> Node | None:
    """The node nearest a point, within reach cells of it."""
    i, j = math.floor(p[0] / CELL), math.floor(p[2] / CELL)
    best = None
    for di in range(-reach, reach + 1):
        for dj in range(-reach, reach + 1):
            for k, h in enumerate(ground.layers.get((i + di, j + dj), [])):
                d = math.hypot(di, dj) + abs(h - p[1]) * 0.5
                if best is None or d < best[0]:
                    best = (d, (i + di, j + dj, k))
    return best[1] if best else None


def find_path(
    ground: Terrain, clear: dict[Node, float], start: Node, goal: Node
) -> list[Point3] | None:
    """The cheapest path from start to goal, as the middles of its cells at
    their heights, or None if there is none."""

    def estimate(node: Node) -> float:
        return math.hypot(node[0] - goal[0], node[1] - goal[1]) * CELL

    queue = [(estimate(start), 0.0, start)]
    came: dict[Node, Node | None] = {start: None}
    cost = {start: 0.0}
    while queue:
        _, g, node = heapq.heappop(queue)
        if node == goal:
            path = []
            while node:
                path.append(node)
                node = came[node]
            return [(centre(i), ground.layers[(i, j)][k], centre(j)) for i, j, k in path[::-1]]
        if g > cost[node]:
            continue
        for step, length in neighbours(ground, node):
            weight = 1.0 + NARROW_COST * max(0.0, 1.0 - clear.get(step, 0.0))
            if step[:2] in ground.boxes:
                weight += BOX_COST
            total = g + length * weight
            if total < cost.get(step, math.inf):
                cost[step] = total
                came[step] = node
                heapq.heappush(queue, (total + estimate(step), total, step))
    return None
