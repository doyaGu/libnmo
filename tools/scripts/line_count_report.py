#!/usr/bin/env python3
"""Report tracked text lines per top-level directory as a Markdown table.

Counts blobs in a git revision, so the numbers do not depend on build output
or untracked files.  Binary blobs (containing NUL bytes) and submodules are
skipped.  With --base, a delta column shows growth against another revision,
which is how CI makes size changes visible per pull request.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ROOT_GROUP = "(root)"


def git(*args: str, data: bytes | None = None) -> bytes:
    return subprocess.run(
        ["git", *args], cwd=ROOT, input=data, check=True, stdout=subprocess.PIPE
    ).stdout


def count_revision(revision: str) -> dict[str, tuple[int, int]]:
    """Return {group: (files, lines)} for text blobs in revision."""
    entries = []
    for record in git("ls-tree", "-r", "-z", revision).split(b"\0"):
        if not record:
            continue
        meta, path = record.split(b"\t", 1)
        _mode, kind, sha = meta.split()
        if kind == b"blob":
            entries.append((path.decode("utf-8", "surrogateescape"), sha))

    output = git("cat-file", "--batch", data=b"".join(sha + b"\n" for _, sha in entries))
    totals: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    offset = 0
    for path, _sha in entries:
        header_end = output.index(b"\n", offset)
        size = int(output[offset:header_end].split()[2])
        body = output[header_end + 1:header_end + 1 + size]
        offset = header_end + 1 + size + 1
        if b"\0" in body:
            continue
        group = path.split("/", 1)[0] if "/" in path else ROOT_GROUP
        lines = body.count(b"\n") + (1 if body and not body.endswith(b"\n") else 0)
        totals[group][0] += 1
        totals[group][1] += lines
    return {group: (files, lines) for group, (files, lines) in totals.items()}


def signed(value: int) -> str:
    return f"{value:+d}" if value else "0"


def render(current: dict[str, tuple[int, int]],
           base: dict[str, tuple[int, int]] | None) -> str:
    groups = sorted(set(current) | set(base or {}),
                    key=lambda group: (-current.get(group, (0, 0))[1], group))
    header = "| Directory | Files | Lines |"
    rule = "| --- | ---: | ---: |"
    if base is not None:
        header += " Delta |"
        rule += " ---: |"
    rows = [header, rule]
    total_files = total_lines = total_base = 0
    for group in groups:
        files, lines = current.get(group, (0, 0))
        row = f"| {group} | {files} | {lines} |"
        if base is not None:
            base_lines = base.get(group, (0, 0))[1]
            total_base += base_lines
            row += f" {signed(lines - base_lines)} |"
        rows.append(row)
        total_files += files
        total_lines += lines
    total = f"| **Total** | {total_files} | {total_lines} |"
    if base is not None:
        total += f" {signed(total_lines - total_base)} |"
    rows.append(total)
    return "\n".join(rows) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", default="HEAD", help="revision to count (default: HEAD)")
    parser.add_argument("--base", help="revision to compare against")
    args = parser.parse_args(argv)

    try:
        current = count_revision(args.revision)
        base = count_revision(args.base) if args.base else None
    except subprocess.CalledProcessError as error:
        print(f"error: git failed: {error}", file=sys.stderr)
        return 1
    sys.stdout.write(render(current, base))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
