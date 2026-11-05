#!/usr/bin/env python3
"""List author-facing native resources, or inspect one complete description."""

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ballance.game.resources import catalog


def entry(spec):
    placement = spec.get('placement', {})
    if 'route' in placement:
        return 'route: ' + json.dumps(placement['route'])
    if 'module' in placement:
        return 'route module: ' + placement['module']
    if 'element' in placement:
        return 'elements: at [along, right], up, pile (boxes only)'
    if placement.get('authorable'):
        return 'scenery.instances: parent, at [along, right, up]'
    if placement.get('role') == 'pole':
        return 'scenery.finish_poles: balloon-relative offsets'
    if placement.get('generated_by'):
        return 'automatic: ' + placement['generated_by']
    return 'automatic: final sector balloon'


def main():
    descriptions = catalog()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        'name', nargs='?', choices=sorted(descriptions), help='Inspect one resource'
    )
    parser.add_argument('--json', action='store_true', help='Emit complete descriptions as JSON')
    args = parser.parse_args()
    selected = {args.name: descriptions[args.name]} if args.name else descriptions
    if args.json or args.name:
        print(json.dumps(selected, indent=2))
    else:
        for name, spec in sorted(selected.items()):
            print(f'{name:20s} {entry(spec)}')
        print(f'{len(selected)} resources; native motion coverage is reported separately.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
