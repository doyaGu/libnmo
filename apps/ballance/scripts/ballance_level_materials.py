#!/usr/bin/env python3
"""Extract the materials of Ballance's levels as project manifest materials.

The 13 original levels share one set of materials: each name has the same
colors, render settings and texture in every level it appears in.  This
script reads them from the level files with the nmo CLI and writes them as
the root "materials" of a project manifest (see docs/ballance-level-format.md),
so a generated level can use the game's own materials by name
(apps/ballance/data/level_materials.json is its output).

When a material differs between levels, the variant most levels use is kept
and the others are reported on stderr.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.nmo import Nmo, add_nmo_argument  # noqa: E402
from ballance.extract.materials import extract_materials  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract Ballance's level materials as project manifest materials."
    )
    parser.add_argument('game', help="Ballance installation folder (holds '3D Entities')")
    parser.add_argument('-o', '--output', required=True, help='JSON file to write')
    add_nmo_argument(parser)
    args = parser.parse_args()

    levels = sorted((Path(args.game) / '3D Entities' / 'Level').glob('Level_*.NMO'))
    if not levels:
        print(f'no Level_*.NMO under {args.game}', file=sys.stderr)
        return 2
    materials = extract_materials(levels, Nmo(args.nmo))
    Path(args.output).write_text(
        json.dumps({'materials': materials}, indent=2, ensure_ascii=False) + '\n', encoding='utf-8'
    )
    print(f'{len(materials)} materials from {len(levels)} levels -> {args.output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
