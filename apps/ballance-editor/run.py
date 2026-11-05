#!/usr/bin/env python3
"""Launch the standalone Ballance editor against an external toolchain."""
import argparse
import os
from pathlib import Path
import sys


def main():
    bootstrap = argparse.ArgumentParser(add_help=False)
    bootstrap.add_argument('--toolchain', type=Path,
                           default=os.environ.get('BALLANCE_TOOLCHAIN',
                               Path(__file__).resolve().parent.parent / 'ballance'))
    args, remaining = bootstrap.parse_known_args()
    root = args.toolchain.expanduser().resolve()
    if not (root / 'ballance/core/project.py').is_file():
        bootstrap.error(f'No Ballance toolchain at {root}; use --toolchain PATH or BALLANCE_TOOLCHAIN')
    sys.path.insert(0, str(root))
    from ballance_editor.server import main as serve
    return serve(remaining)


if __name__ == '__main__':
    sys.exit(main())
