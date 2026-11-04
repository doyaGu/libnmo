#!/usr/bin/env python3
"""Check whether a report, trace or preview still matches its recorded inputs."""

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.evidence import inspect  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact', type=Path)
    args = parser.parse_args()
    result = inspect(args.artifact)
    print(json.dumps(result, indent=2))
    return {'current': 0, 'stale': 1, 'unavailable': 2}[result['status']]


if __name__ == '__main__':
    sys.exit(main())
