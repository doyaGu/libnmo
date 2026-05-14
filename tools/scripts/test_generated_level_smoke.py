#!/usr/bin/env python3
"""Self-test for the opt-in generated level smoke helper."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    script_path = Path(__file__).resolve().with_name("generated_level_smoke.py")
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        cmo_path = tmp_path / "level.cmo"
        cmo_path.write_bytes(b"NMO smoke placeholder")
        evidence_dir = tmp_path / "evidence"

        completed = subprocess.run(
            [
                sys.executable,
                str(script_path),
                str(cmo_path),
                "--player",
                sys.executable,
                "--player-arg=-c",
                "--player-arg",
                "import sys; print('fake player', sys.argv[-1])",
                "--timeout",
                "5",
                "--evidence-dir",
                str(evidence_dir),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            print(completed.stdout)
            print(completed.stderr, file=sys.stderr)
            return completed.returncode

        result = json.loads(completed.stdout)
        assert result["ok"] is True
        assert Path(result["cmo"]).is_absolute()
        assert Path(result["player"]).is_absolute()
        assert result["timeout_seconds"] == 5
        assert isinstance(result["command"], list)
        assert result["exit_code"] == 0
        assert result["timed_out"] is False
        assert Path(result["logs"]["stdout"]).is_absolute()
        assert Path(result["logs"]["stderr"]).is_absolute()
        assert Path(result["logs"]["result"]).is_absolute()
        assert Path(result["logs"]["stdout"]).is_file()
        assert Path(result["logs"]["stderr"]).is_file()
        assert Path(result["logs"]["result"]).is_file()
        assert "fake player" in result["stdout_tail"]

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
