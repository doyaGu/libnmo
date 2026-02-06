#!/usr/bin/env python3
"""Generate enum/flags registration helpers from C enum definitions.

Usage examples:
  python tools/scripts/gen_object_enums.py \
    --input include/object/nmo_object_enum_defs.h \
    --output src/object/object_enums.generated.h \
    --overrides tools/scripts/enum_overrides.json \
    --emit-register

The overrides JSON schema:
{
  "enums": {
    "CKAXIS": {
      "kind": "enum",
      "guid": ["0x79465230", "0x62a88af1"],
      "default": "CKAXIS_X"
    },
    "CK_OBJECT_FLAGS": {
      "kind": "flags",
      "guid": ["0x0", "0x0"],
      "default": "0"
    }
  }
}
"""

from __future__ import annotations

import argparse
import ast
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


COMMENT_BLOCK_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
COMMENT_LINE_RE = re.compile(r"//.*?$", re.MULTILINE)

ENUM_BLOCK_RE = re.compile(
    r"typedef\s+enum\s+(?P<tag>\w+)?\s*\{(?P<body>.*?)\}\s*(?P<name>\w+)?\s*;",
    re.DOTALL,
)

ENUM_MEMBER_RE = re.compile(
    r"^(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*(?P<value>.+))?$"
)


@dataclass
class EnumMember:
    name: str
    value_expr: Optional[str]
    value: Optional[int]


@dataclass
class EnumDef:
    name: str
    members: List[EnumMember]


@dataclass
class EnumOverride:
    kind: Optional[str] = None  # "enum" or "flags"
    guid: Optional[Tuple[str, str]] = None
    default: Optional[str] = None


_AST_NUM = getattr(ast, "Num", None)


class SafeEvaluator(ast.NodeVisitor):
    allowed_nodes = (
        ast.Expression,
        ast.BinOp,
        ast.UnaryOp,
        ast.Constant,
        ast.Name,
        ast.LShift,
        ast.RShift,
        ast.BitOr,
        ast.BitAnd,
        ast.BitXor,
        ast.Add,
        ast.Sub,
        ast.Invert,
        ast.USub,
        ast.UAdd,
        ast.ParenExpr if hasattr(ast, "ParenExpr") else ast.expr,
    ) + ((_AST_NUM,) if _AST_NUM is not None else ())

    def __init__(self, names: Dict[str, int]) -> None:
        self._names = names

    def visit(self, node):  # type: ignore[override]
        if not isinstance(node, self.allowed_nodes):
            raise ValueError(f"Unsupported expression: {ast.dump(node)}")
        return super().visit(node)

    def visit_Expression(self, node: ast.Expression) -> int:
        return self.visit(node.body)

    def visit_Name(self, node: ast.Name) -> int:
        if node.id not in self._names:
            raise ValueError(f"Unknown identifier: {node.id}")
        return self._names[node.id]

    def visit_Constant(self, node: ast.Constant) -> int:
        if isinstance(node.value, bool) or not isinstance(node.value, (int, float)):
            raise ValueError("Invalid constant")
        return int(node.value)

    def visit_Num(self, node: ast.Num) -> int:  # pragma: no cover (py<3.8)
        return int(node.n)

    def visit_UnaryOp(self, node: ast.UnaryOp) -> int:
        operand = self.visit(node.operand)
        if isinstance(node.op, ast.USub):
            return -operand
        if isinstance(node.op, ast.UAdd):
            return operand
        if isinstance(node.op, ast.Invert):
            return ~operand
        raise ValueError("Unsupported unary op")

    def visit_BinOp(self, node: ast.BinOp) -> int:
        left = self.visit(node.left)
        right = self.visit(node.right)
        if isinstance(node.op, ast.Add):
            return left + right
        if isinstance(node.op, ast.Sub):
            return left - right
        if isinstance(node.op, ast.LShift):
            return left << right
        if isinstance(node.op, ast.RShift):
            return left >> right
        if isinstance(node.op, ast.BitOr):
            return left | right
        if isinstance(node.op, ast.BitAnd):
            return left & right
        if isinstance(node.op, ast.BitXor):
            return left ^ right
        raise ValueError("Unsupported binary op")


def strip_comments(text: str) -> str:
    text = COMMENT_BLOCK_RE.sub("", text)
    text = COMMENT_LINE_RE.sub("", text)
    return text


def split_members(body: str) -> List[str]:
    parts: List[str] = []
    current: List[str] = []
    depth = 0
    for ch in body:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth = max(0, depth - 1)
        if ch == "," and depth == 0:
            part = "".join(current).strip()
            if part:
                parts.append(part)
            current = []
        else:
            current.append(ch)
    tail = "".join(current).strip()
    if tail:
        parts.append(tail)
    return parts


def parse_enums(text: str) -> List[EnumDef]:
    text = strip_comments(text)
    enums: List[EnumDef] = []

    for match in ENUM_BLOCK_RE.finditer(text):
        tag = match.group("tag")
        name = match.group("name")
        enum_name = name or tag
        if not enum_name:
            continue
        body = match.group("body")
        members: List[EnumMember] = []
        last_value: Optional[int] = None
        value_map: Dict[str, int] = {}

        for raw in split_members(body):
            m = ENUM_MEMBER_RE.match(raw.strip())
            if not m:
                continue
            member_name = m.group("name")
            value_expr = m.group("value")
            value = None
            if value_expr is None:
                if last_value is None:
                    value = 0
                else:
                    value = last_value + 1
            else:
                value_expr = value_expr.strip()
                try:
                    node = ast.parse(value_expr, mode="eval")
                    value = SafeEvaluator(value_map).visit(node)
                except Exception:
                    value = None
            member = EnumMember(member_name, value_expr, value)
            members.append(member)
            if value is not None:
                last_value = value
                value_map[member_name] = value

        if members:
            enums.append(EnumDef(enum_name, members))

    return enums


def is_power_of_two(value: int) -> bool:
    return value > 0 and (value & (value - 1)) == 0


def classify_enum(enum_def: EnumDef, override: Optional[EnumOverride]) -> str:
    if override and override.kind:
        return override.kind

    name_upper = enum_def.name.upper()
    if "FLAGS" in name_upper or "MASK" in name_upper or "BIT" in name_upper:
        return "flags"

    expressions = [m.value_expr or "" for m in enum_def.members]
    if any("|" in expr or "<<" in expr for expr in expressions):
        return "flags"

    values = [m.value for m in enum_def.members if m.value is not None]
    if not values:
        return "enum"

    sorted_values = sorted(set(values))
    if len(sorted_values) == len(values):
        start = sorted_values[0]
        if start in (0, 1) and sorted_values == list(range(start, start + len(sorted_values))):
            return "enum"

    if all(v == 0 or is_power_of_two(v) for v in values):
        return "flags"

    return "enum"


def macro_name(enum_name: str, prefix: str) -> str:
    name = re.sub(r"[^A-Za-z0-9_]", "_", enum_name)
    return f"{prefix}{name.upper()}"


def load_overrides(path: Optional[Path]) -> Dict[str, EnumOverride]:
    if not path:
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    result: Dict[str, EnumOverride] = {}
    enums = data.get("enums", {})
    for name, raw in enums.items():
        override = EnumOverride()
        kind = raw.get("kind")
        if kind:
            override.kind = kind
        guid = raw.get("guid")
        if isinstance(guid, list) and len(guid) == 2:
            override.guid = (str(guid[0]), str(guid[1]))
        default = raw.get("default")
        if default is not None:
            override.default = str(default)
        result[name] = override
    return result


def emit_header_guard(lines: List[str], guard: str, body: Iterable[str]) -> None:
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}")
    lines.append("")
    lines.extend(body)
    lines.append("")
    lines.append(f"#endif /* {guard} */")


def generate(
    enums: List[EnumDef],
    overrides: Dict[str, EnumOverride],
    emit_register: bool,
) -> str:
    lines: List[str] = []
    lines.append("/* Auto-generated by tools/scripts/gen_object_enums.py */")
    lines.append("/* Do not edit by hand. */")
    lines.append("")

    lines.append("#ifdef NMO_ENUMS_EMIT_DECLS")
    for enum_def in enums:
        override = overrides.get(enum_def.name)
        kind = classify_enum(enum_def, override)
        macro_prefix = "NMO_FLAGS_BITS_" if kind == "flags" else "NMO_ENUM_VALUES_"
        macro = macro_name(enum_def.name, macro_prefix)
        members = enum_def.members
        if kind == "flags":
            members = [
                m for m in enum_def.members
                if m.value is not None and is_power_of_two(m.value)
            ]
            seen_masks = set()
            deduped: List[EnumMember] = []
            for member in members:
                if member.value in seen_masks:
                    continue
                seen_masks.add(member.value)
                deduped.append(member)
            members = deduped
        if members:
            lines.append(f"#define {macro}(X) \\")
            for i, member in enumerate(members):
                suffix = ', \\' if i < len(members) - 1 else ""
                lines.append(f"    X({member.name}, {member.name}){suffix}")
        else:
            lines.append(f"#define {macro}(X)")
        lines.append("")

    lines.append("/* Definitions */")
    for enum_def in enums:
        override = overrides.get(enum_def.name)
        kind = classify_enum(enum_def, override)
        macro_prefix = "NMO_FLAGS_BITS_" if kind == "flags" else "NMO_ENUM_VALUES_"
        macro = macro_name(enum_def.name, macro_prefix)
        guid_d1 = override.guid[0] if override and override.guid else "0"
        guid_d2 = override.guid[1] if override and override.guid else "0"
        if override and override.default is not None:
            default_value = override.default
        elif kind == "flags":
            default_value = "0"
        else:
            default_value = enum_def.members[0].name
        def_macro = "NMO_FLAGS_DEF" if kind == "flags" else "NMO_ENUM_DEF"
        lines.append(
            f"{def_macro}({enum_def.name}, {guid_d1}, {guid_d2}, {default_value}, {macro});"
        )
    lines.append("#endif /* NMO_ENUMS_EMIT_DECLS */")
    lines.append("")

    if emit_register:
        lines.append("#ifdef NMO_ENUMS_EMIT_REGISTRATIONS")
        lines.append("/* Registration */")
        for enum_def in enums:
            kind = classify_enum(enum_def, overrides.get(enum_def.name))
            register = "NMO_REGISTER_FLAGS" if kind == "flags" else "NMO_REGISTER_ENUM"
            lines.append(f"{register}(registry, {enum_def.name});")
        lines.append("#endif /* NMO_ENUMS_EMIT_REGISTRATIONS */")
        lines.append("")

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", required=True, help="Header file(s) to parse")
    parser.add_argument("--output", help="Output file (defaults to stdout)")
    parser.add_argument("--overrides", help="JSON overrides file")
    parser.add_argument("--emit-register", action="store_true", help="Emit registration calls")
    parser.add_argument("--header-guard", help="Wrap output with a header guard")
    args = parser.parse_args()

    overrides = load_overrides(Path(args.overrides)) if args.overrides else {}

    enums: List[EnumDef] = []
    for input_path in args.input:
        text = Path(input_path).read_text(encoding="utf-8")
        enums.extend(parse_enums(text))

    output = generate(enums, overrides, args.emit_register)

    if args.header_guard:
        body = output.splitlines()
        lines: List[str] = []
        emit_header_guard(lines, args.header_guard, body)
        output = "\n".join(lines)

    if args.output:
        Path(args.output).write_text(output, encoding="utf-8")
    else:
        print(output)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
