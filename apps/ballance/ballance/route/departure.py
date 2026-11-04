"""The space reserved for the native balloon's visible departure."""

from ..core.geometry import at
from ..game.resources import resource


def boxes(balloon):
    """Conservative world boxes, split along the flight to preserve heading.

    Include the entire basket, folding bridge, ropes and small balloons,
    rather than just the player's centre or the parked marker mesh.
    """
    if balloon is None:
        return []
    (x, y, z), heading = balloon
    description = resource('Balloon')['runtime']['departure']
    back, forward, low, high, left, right = description['bounds']
    result = []
    a = back
    while a < forward:
        b = min(a + description['step'], forward)
        corners = [at((x, z), heading, along, side)
                   for along in (a, b) for side in (left, right)]
        result.append((min(p[0] for p in corners), max(p[0] for p in corners),
                       y + low, y + high,
                       min(p[1] for p in corners), max(p[1] for p in corners)))
        a = b
    return result
