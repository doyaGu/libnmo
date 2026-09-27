#!/usr/bin/env python3
"""Generate and verify the Lua table pushers for stable consumer view structs.

Each spec names a struct from a header tagged NMO_API_TIER_STABLE_CONSUMER.
The struct body is parsed from the header, so the Lua table shape follows the
C declaration: scalars become integer/number/boolean fields, strings become
optional string fields, and nested structs become sub-tables.  Functions that
need callbacks, ownership transfer, or computed fields stay hand-written.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUTPUT_C = ROOT / "src" / "lua" / "lua_pushers_generated.c"
OUTPUT_H = ROOT / "src" / "lua" / "lua_pushers_generated.h"
STABLE_TIER = "NMO_API_TIER_STABLE_CONSUMER"
GENERATOR = "tools/scripts/gen_lua_pushers.py"

INTEGER_TYPES = {
    "size_t",
    "int",
    "int8_t",
    "int16_t",
    "int32_t",
    "int64_t",
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "uint64_t",
    "nmo_object_id_t",
    "nmo_class_id_t",
    "nmo_status_t",
}
NUMBER_TYPES = {"float", "double"}
STRING_TYPES = {"const char *", "char *"}


@dataclass(frozen=True)
class PusherSpec:
    header: str
    type_name: str
    function: str
    # C fields left out of the Lua table.
    skip: tuple[str, ...] = ()
    # Nested struct fields pushed only when a boolean field is set:
    # C field -> (Lua key, presence field).
    optional: dict[str, tuple[str, str]] = field(default_factory=dict)


SPECS = [
    PusherSpec(
        header="include/format/nmo_interface_view.h",
        type_name="nmo_interface_body_view_t",
        function="nmo_lua_push_interface_body_view",
    ),
    PusherSpec(
        header="include/format/nmo_interface_view.h",
        type_name="nmo_interface_view_t",
        function="nmo_lua_push_interface_view",
    ),
    PusherSpec(
        header="include/behavior/nmo_behavior_query.h",
        type_name="nmo_behavior_script_view_t",
        function="nmo_lua_push_behavior_script_view",
    ),
    PusherSpec(
        header="include/behavior/nmo_behavior_view.h",
        type_name="nmo_behavior_view_t",
        function="nmo_lua_push_behavior_view",
        optional={"interface_view": ("interface", "interface_available")},
    ),
    PusherSpec(
        header="include/behavior/nmo_behavior_view.h",
        type_name="nmo_behavior_boundary_view_t",
        function="nmo_lua_push_behavior_boundary_view",
    ),
    PusherSpec(
        header="include/document/nmo_document_stats.h",
        type_name="nmo_file_stats_t",
        function="nmo_lua_push_file_stats",
        skip=("objects.by_class",),
    ),
]


@dataclass
class Field:
    name: str
    type_name: str
    # Anonymous nested struct members, when type_name == "struct".
    members: list["Field"] | None = None
    is_array: bool = False


class GeneratorError(Exception):
    pass


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def split_declarations(body: str) -> list[str]:
    declarations = []
    depth = 0
    start = 0
    for index, char in enumerate(body):
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        elif char == ";" and depth == 0:
            declaration = body[start:index].strip()
            if declaration:
                declarations.append(declaration)
            start = index + 1
    if body[start:].strip():
        raise GeneratorError(f"unterminated declaration: {body[start:].strip()}")
    return declarations


def parse_fields(body: str, where: str) -> list[Field]:
    fields = []
    for declaration in split_declarations(body):
        declaration = " ".join(declaration.split())
        nested = re.fullmatch(r"struct \{(.*)\} (\w+)", declaration)
        if nested:
            fields.append(
                Field(
                    name=nested.group(2),
                    type_name="struct",
                    members=parse_fields(nested.group(1), f"{where}.{nested.group(2)}"),
                )
            )
            continue
        match = re.fullmatch(r"(.+?)\s*(\*?)\s*(\w+)\s*(\[[^\]]*\])?", declaration)
        if not match:
            raise GeneratorError(f"{where}: cannot parse field '{declaration}'")
        base, pointer, name, array = match.groups()
        type_name = f"{base.strip()} *" if pointer else base.strip()
        fields.append(Field(name=name, type_name=type_name, is_array=array is not None))
    return fields


def parse_struct(header: Path, type_name: str) -> list[Field]:
    text = strip_comments(header.read_text(encoding="utf-8"))
    if STABLE_TIER not in text:
        raise GeneratorError(f"{header.relative_to(ROOT)} is not tagged {STABLE_TIER}")
    tag = type_name[:-2] if type_name.endswith("_t") else type_name
    match = re.search(rf"typedef\s+struct\s+{re.escape(tag)}\s*\{{", text)
    if not match:
        raise GeneratorError(f"{type_name} not found in {header.relative_to(ROOT)}")
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                closing = re.match(rf"\}}\s*{re.escape(type_name)}\s*;", text[index:])
                if not closing:
                    raise GeneratorError(f"{type_name}: unexpected typedef tail")
                return parse_fields(text[match.end():index], type_name)
    raise GeneratorError(f"{type_name}: unbalanced braces")


class Renderer:
    def __init__(self, specs: list[PusherSpec]):
        self.specs = specs
        self.functions = {spec.type_name: spec.function for spec in specs}

    def field_lines(self, spec: PusherSpec, fields: list[Field], access: str,
                    path: str, indent: str) -> list[str]:
        lines = []
        for item in fields:
            dotted = f"{path}{item.name}"
            member = f"{access}{item.name}"
            if dotted in spec.skip:
                continue
            if item.is_array:
                raise GeneratorError(f"{spec.type_name}.{dotted}: arrays need skip or a hand-written pusher")
            if item.members is not None:
                kept = [m for m in item.members if f"{dotted}.{m.name}" not in spec.skip]
                lines.append(f"{indent}lua_createtable(state, 0, {len(kept)});")
                lines.extend(self.field_lines(spec, item.members, f"{member}.", f"{dotted}.", indent))
                lines.append(f'{indent}lua_setfield(state, -2, "{item.name}");')
            elif item.type_name in self.functions:
                key = item.name
                call = f"{self.functions[item.type_name]}(state, &{member});"
                if dotted in spec.optional:
                    key, presence = spec.optional[dotted]
                    lines.append(f"{indent}if ({access}{presence}) {{")
                    lines.append(f"{indent}    {call}")
                    lines.append(f'{indent}    lua_setfield(state, -2, "{key}");')
                    lines.append(f"{indent}}}")
                else:
                    lines.append(f"{indent}{call}")
                    lines.append(f'{indent}lua_setfield(state, -2, "{key}");')
            elif item.type_name == "bool":
                lines.append(f'{indent}nmo_lua_set_boolean_field(state, "{item.name}", {member});')
            elif item.type_name in INTEGER_TYPES:
                lines.append(f'{indent}nmo_lua_set_integer_field(state, "{item.name}", (lua_Integer){member});')
            elif item.type_name in NUMBER_TYPES:
                lines.append(f'{indent}nmo_lua_set_number_field(state, "{item.name}", (lua_Number){member});')
            elif item.type_name in STRING_TYPES:
                lines.append(f'{indent}nmo_lua_set_optional_string_field(state, "{item.name}", {member});')
            else:
                raise GeneratorError(f"{spec.type_name}.{dotted}: unsupported type '{item.type_name}'")
        return lines

    def signature(self, spec: PusherSpec) -> str:
        return f"void {spec.function}(lua_State *state, const {spec.type_name} *value)"

    def render_c(self) -> str:
        headers = sorted({spec.header.removeprefix("include/") for spec in self.specs})
        out = [
            f"/* Auto-generated by {GENERATOR}; do not edit by hand. */",
            "",
            '#include "lua_pushers_generated.h"',
            "",
            '#include "lua_bindings_internal.h"',
            "",
        ]
        out.extend(f'#include "{header}"' for header in headers)
        for spec in self.specs:
            fields = parse_struct(ROOT / spec.header, spec.type_name)
            count = sum(1 for item in fields if item.name not in spec.skip)
            out.append("")
            out.append(self.signature(spec))
            out.append("{")
            out.append(f"    lua_createtable(state, 0, {count});")
            out.extend(self.field_lines(spec, fields, "value->", "", "    "))
            out.append("}")
        return "\n".join(out) + "\n"

    def render_h(self) -> str:
        headers = sorted({spec.header.removeprefix("include/") for spec in self.specs})
        out = [
            f"/* Auto-generated by {GENERATOR}; do not edit by hand. */",
            "#ifndef NMO_LUA_PUSHERS_GENERATED_H",
            "#define NMO_LUA_PUSHERS_GENERATED_H",
            "",
            '#include "lua.h"',
            "",
        ]
        out.extend(f'#include "{header}"' for header in headers)
        out.append("")
        out.extend(f"{self.signature(spec)};" for spec in self.specs)
        out.append("")
        out.append("#endif /* NMO_LUA_PUSHERS_GENERATED_H */")
        return "\n".join(out) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="write generated pusher files")
    parser.add_argument("--check", action="store_true", help="verify generated files match the repository")
    args = parser.parse_args(argv)
    if not args.write and not args.check:
        parser.error("pass --write or --check")

    renderer = Renderer(SPECS)
    try:
        rendered = {OUTPUT_C: renderer.render_c(), OUTPUT_H: renderer.render_h()}
    except GeneratorError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    if args.write:
        for path, content in rendered.items():
            path.write_text(content, encoding="utf-8", newline="\n")

    if args.check:
        stale = [
            path for path, content in rendered.items()
            if not path.exists() or path.read_text(encoding="utf-8") != content
        ]
        for path in stale:
            print(f"{path.relative_to(ROOT)} is out of date; regenerate with {GENERATOR} --write",
                  file=sys.stderr)
        if stale:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
