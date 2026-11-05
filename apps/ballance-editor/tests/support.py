"""Resolve this application's external toolchain dependency for its tests."""

import os
from pathlib import Path
import sys

APP_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = (
    Path(os.environ.get('BALLANCE_TOOLCHAIN', APP_ROOT.parent / 'ballance')).expanduser().resolve()
)
sys.path.insert(0, str(APP_ROOT))
sys.path.insert(0, str(TOOLCHAIN_ROOT))
