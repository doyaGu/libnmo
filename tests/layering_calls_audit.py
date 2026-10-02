#!/usr/bin/env python3
"""Fail when a source file uses a function defined in a higher layer.

layering_audit.cmake checks includes, but a header can pull in declarations of
other layers transitively, so a file can call upward without an upward include
of its own. This audit works on the functions themselves: every function with
external linkage defined in src/<layer>/ belongs to that layer, and no file of
a lower layer may name it (a call, or taking its address).

The layer order is read from layering_audit.cmake, so both audits use the same
stack.

Usage: layering_calls_audit.py [SOURCE_DIR]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Comments, string literals and character literals, matched in one pass so that
# "//" inside a string is not taken for a comment.
NOISE_RE = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
# A definition starts at column 0 (statements inside a body are indented), has
# a return type (possibly on its own line), a name, a parameter list and then
# the body.
DEFINITION_RE = re.compile(
    r"^(static\s+)?[A-Za-z_][\w \t\n\*]*?\b([A-Za-z_]\w*)\s*\([^;{}()]*(?:\([^;{}]*\)[^;{}()]*)*\)\s*\{",
    re.M,
)
IDENTIFIER_RE = re.compile(r"\b[A-Za-z_]\w*\b")
MACRO_RE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)\(([^)]*)\)((?:[^\n]*\\\n)*[^\n]*)", re.M)
INVOCATION_RE = re.compile(r"^([A-Za-z_]\w*)\s*\(", re.M)
MAX_EXPANSION_DEPTH = 8
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "do", "else"}


def read_layers(source_dir: Path) -> list[str]:
    text = (source_dir / "tests" / "layering_audit.cmake").read_text(encoding="utf-8")
    match = re.search(r"^set\(layers ([a-z ]+)\)", text, re.M)
    if not match:
        raise SystemExit("layering_calls_audit: layer list not found in layering_audit.cmake")
    return match.group(1).split()


def strip_code(text: str) -> str:
    def blank(match: re.Match[str]) -> str:
        token = match.group(0)
        if token.startswith(("/*", "//")):
            # Keep the line structure so column-0 definitions stay at column 0.
            return re.sub(r"[^\n]", " ", token)
        return '""'

    return NOISE_RE.sub(blank, text)


def read_macros(paths: list[Path]) -> dict[str, tuple[list[str], str]]:
    """Function-like macros: name -> (parameters, body with its lines dedented)."""
    macros: dict[str, tuple[list[str], str]] = {}
    for path in paths:
        text = strip_code(path.read_text(encoding="utf-8", errors="replace"))
        for match in MACRO_RE.finditer(text):
            params = [param.strip() for param in match.group(2).split(",") if param.strip()]
            lines = [line.rstrip().rstrip("\\").rstrip() for line in match.group(3).split("\n")]
            indents = [len(line) - len(line.lstrip()) for line in lines if line.strip()]
            indent = min(indents, default=0)
            body = "\n".join(line[indent:] for line in lines)
            macros.setdefault(match.group(1), (params, body))
    return macros


def split_arguments(text: str, start: int) -> list[str] | None:
    """Arguments of an invocation whose "(" ends just before start."""
    args, depth, current = [], 0, start
    for index in range(start, len(text)):
        char = text[index]
        if char == "(":
            depth += 1
        elif char == ")":
            if depth == 0:
                args.append(text[current:index].strip())
                return args
            depth -= 1
        elif char == "," and depth == 0:
            args.append(text[current:index].strip())
            current = index + 1
    return None


def substitute(macro: tuple[list[str], str], args: list[str]) -> str:
    params, body = macro
    values = dict(zip(params, args))
    if params and params[-1] == "...":
        values["__VA_ARGS__"] = ", ".join(args[len(params) - 1:])
    body = re.sub(r"(?<![#\w])#(?!#)\s*\w+", '""', body)  # stringizing
    body = IDENTIFIER_RE.sub(lambda match: values.get(match.group(0), match.group(0)), body)
    return re.sub(r"\s*##\s*", "", body)


def expand_file_scope(text: str, macros: dict[str, tuple[list[str], str]], depth: int = 0) -> str:
    """Expansions of the macros invoked at the start of a line, recursively."""
    if depth > MAX_EXPANSION_DEPTH:
        return ""
    pieces = []
    for match in INVOCATION_RE.finditer(text):
        macro = macros.get(match.group(1))
        if macro is None:
            continue
        args = split_arguments(text, match.end())
        if args is None:
            continue
        body = substitute(macro, args)
        pieces.append(body)
        pieces.append(expand_file_scope(body, macros, depth + 1))
    return "\n".join(pieces)


def main() -> int:
    source_dir = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[1])
    layers = read_layers(source_dir)
    rank = {layer: index for index, layer in enumerate(layers)}

    macros = read_macros(sorted((source_dir / "src").rglob("*.[ch]"))
                         + sorted((source_dir / "include").rglob("*.h")))

    files: dict[str, tuple[str, str]] = {}
    for path in sorted((source_dir / "src").rglob("*.c")):
        layer = path.relative_to(source_dir / "src").parts[0]
        if layer in rank:
            relative = path.relative_to(source_dir).as_posix()
            text = strip_code(path.read_text(encoding="utf-8", errors="replace"))
            files[relative] = (layer, text + "\n" + expand_file_scope(text, macros) + "\n")

    owner: dict[str, str] = {}  # function -> defining file
    local: dict[str, set[str]] = {}  # file -> its static functions
    for relative, (_, text) in files.items():
        local[relative] = set()
        for match in DEFINITION_RE.finditer(text):
            name = match.group(2)
            if name in KEYWORDS:
                continue
            if match.group(1):
                local[relative].add(name)
            else:
                owner.setdefault(name, relative)

    violations = []
    for relative, (layer, text) in files.items():
        for name in sorted(set(IDENTIFIER_RE.findall(text)) - local[relative]):
            defining_file = owner.get(name)
            if defining_file is None:
                continue
            defining_layer = files[defining_file][0]
            if rank[defining_layer] > rank[layer]:
                violations.append(
                    f"{relative} ({layer}) uses {name}, defined in {defining_file} ({defining_layer})"
                )

    if violations:
        print("Layering audit failed: function(s) of a higher layer used")
        for line in violations:
            print("  " + line)
        print("Move the function down the stack, or let the higher layer pass it in "
              "(a callback or an interface struct).")
        return 1
    print(f"Layering calls audit passed ({len(owner)} functions, {len(files)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
