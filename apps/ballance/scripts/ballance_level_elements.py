#!/usr/bin/env python3
"""Export the meshes of Ballance's level elements as OBJ files.

One mesh of each marker and element kind, and the SkyLayer cloud plane, as
<Kind>.obj (with .mtl), taken from the original levels; and the floors cut
out of them that generated levels need: the start's and the checkpoints'
floors, a fan's housing, the drawbridge's and the switch's floors and
stoppers, the plazas of the pushers and the stone on a box, and the scenery
(pillar, lantern tower, striped pole).  What each
is and how it is cut: ballance.extract.elements.

The meshes are the game's own, so they are written from a local installation
and not kept with libnmo.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo, add_nmo_argument  # noqa: E402
from ballance.extract.elements import Levels, every_kind, export_elements  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export Ballance's level element meshes as OBJ files.")
    parser.add_argument("game", help="Ballance installation folder (holds '3D Entities')")
    parser.add_argument("-o", "--output", required=True, help="Folder for <Kind>.obj files")
    add_nmo_argument(parser)
    args = parser.parse_args()

    levels = Levels(Path(args.game), Nmo(args.nmo))
    if not levels.files:
        print(f"no Level_*.NMO under {args.game}", file=sys.stderr)
        return 2
    found = export_elements(levels, Path(args.output))
    every = every_kind()
    for kind in every:
        print(f"{kind:16s} {found.get(kind, 'not found')}")
    return 0 if len(found) == len(every) else 1


if __name__ == "__main__":
    sys.exit(main())
