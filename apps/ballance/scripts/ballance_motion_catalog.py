#!/usr/bin/env python3
"""Refresh measured native motion bounds from a local Ballance installation."""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ballance.core.nmo import Nmo, add_nmo_argument
from ballance.core.project import write_json
from ballance.extract.motion import measure

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    add_nmo_argument(parser)
    args = parser.parse_args()
    write_json(args.out, measure(args.game, Nmo(args.nmo)))
    print(args.out)
