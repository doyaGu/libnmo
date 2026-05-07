#!/usr/bin/env python3
"""Opt-in Virtools Player smoke helper for generated CMO files."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path


def tail_text(text: str, limit: int = 4000) -> str:
    if len(text) <= limit:
        return text
    return text[-limit:]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Load a generated CMO in an explicitly configured Virtools Player."
    )
    parser.add_argument("cmo", help="Generated CMO file to load")
    parser.add_argument(
        "--player",
        default=os.environ.get("NMO_PLAYER_PATH"),
        help="Virtools Player executable path, or NMO_PLAYER_PATH",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=30.0,
        help="Maximum seconds to wait for Player exit",
    )
    parser.add_argument(
        "--frame-evidence-file",
        help="Optional file produced by an external wrapper to indicate frames rendered",
    )
    parser.add_argument(
        "--evidence-dir",
        help="Optional directory where stdout, stderr, and result JSON evidence are written",
    )
    parser.add_argument(
        "--strict-exit",
        action="store_true",
        help="Return a failing status when the advisory smoke result is not ok",
    )
    parser.add_argument(
        "--player-arg",
        action="append",
        default=[],
        help="Additional argument passed to Player before the CMO path",
    )
    args = parser.parse_args()

    cmo_path = Path(args.cmo)
    player_path = Path(args.player) if args.player else None
    result = {
        "ok": False,
        "cmo": str(cmo_path),
        "player": str(player_path) if player_path else None,
        "command": None,
        "exit_code": None,
        "timed_out": False,
        "elapsed_seconds": None,
        "frame_evidence": None,
        "logs": None,
        "stdout_tail": "",
        "stderr_tail": "",
    }

    if not cmo_path.is_file():
        result["error"] = "CMO file does not exist"
        print(json.dumps(result, indent=2))
        return 2
    if player_path is None or not player_path.is_file():
        result["error"] = "Player path is not configured or does not exist"
        print(json.dumps(result, indent=2))
        return 2

    command = [str(player_path), *args.player_arg, str(cmo_path)]
    result["command"] = command
    evidence_dir = Path(args.evidence_dir) if args.evidence_dir else None
    if evidence_dir is not None:
        evidence_dir.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    stdout_text = ""
    stderr_text = ""
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=args.timeout,
        )
        result["exit_code"] = completed.returncode
        stdout_text = completed.stdout
        stderr_text = completed.stderr
    except subprocess.TimeoutExpired as exc:
        result["timed_out"] = True
        stdout_text = exc.stdout or ""
        stderr_text = exc.stderr or ""
    finally:
        result["elapsed_seconds"] = round(time.monotonic() - started, 3)

    result["stdout_tail"] = tail_text(stdout_text)
    result["stderr_tail"] = tail_text(stderr_text)
    if evidence_dir is not None:
        stdout_path = evidence_dir / "player_stdout.log"
        stderr_path = evidence_dir / "player_stderr.log"
        stdout_path.write_text(stdout_text, encoding="utf-8", errors="replace")
        stderr_path.write_text(stderr_text, encoding="utf-8", errors="replace")
        result["logs"] = {
            "stdout": str(stdout_path),
            "stderr": str(stderr_path),
        }

    if args.frame_evidence_file:
        evidence = Path(args.frame_evidence_file)
        result["frame_evidence"] = {
            "path": str(evidence),
            "exists": evidence.is_file(),
            "size": evidence.stat().st_size if evidence.is_file() else 0,
        }

    result["ok"] = (
        result["exit_code"] == 0
        and not result["timed_out"]
        and (
            result["frame_evidence"] is None
            or result["frame_evidence"]["exists"]
        )
    )
    if evidence_dir is not None:
        result_path = evidence_dir / "smoke_result.json"
        result["logs"]["result"] = str(result_path)
        result_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["ok"] or not args.strict_exit else 1


if __name__ == "__main__":
    sys.exit(main())
