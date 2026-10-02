#!/usr/bin/env python3
"""Generate and verify the Lua wrappers of the edit plan builders.

`nmo.plan` and the script edit handle of `nmo.behavior` both expose the
`nmo_edit_plan_add_*()` builders. Each wrapper checks its handle, reads its
arguments by position and calls the builder; the wrappers whose arguments map
one to one onto the builder's parameters are generated from the specs below.
The parameter names and types come from include/edit/nmo_edit_plan.h, so a
builder whose signature changes no longer matches its spec and generation
fails. Wrappers that parse enums, GUIDs, handle references or option tables
stay hand-written.

The output is an include file per binding file; each binding file includes it
where its hand-written wrappers are, so the wrappers stay static.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "include" / "edit" / "nmo_edit_plan.h"
GENERATOR = "tools/scripts/gen_lua_edit_bindings.py"

# Argument kinds: how a builder parameter is read from Lua, and the C types it
# may have. Kinds without a Lua argument pass a constant.
ARG_KINDS = {
    "id": ("(nmo_object_id_t)luaL_checkinteger(state, {index})", {"nmo_object_id_t"}),
    "u32": ("(uint32_t)luaL_checkinteger(state, {index})", {"uint32_t"}),
    "opt_u32": ("(uint32_t)luaL_optinteger(state, {index}, 0)", {"uint32_t"}),
    "flags": ("nmo_lua_behavior_optional_flags(state, {index}, 0u)", {"uint32_t"}),
    "string": ("luaL_checkstring(state, {index})", {"const char *"}),
    "bool": ("lua_toboolean(state, {index}) != 0", {"bool"}),
    "null": ("NULL", None),
}


@dataclass(frozen=True)
class Family:
    # Include file, relative to the repository root.
    output: str
    handle_decl: str
    handle_check: str
    handle_error: str
    plan_expr: str


FAMILIES = {
    "plan": Family(
        output="src/lua/lua_bindings_plan_ops.generated.inc",
        handle_decl="nmo_edit_plan_t *plan = NULL;",
        handle_check="nmo_lua_check_edit_plan_handle(state, 1, &plan)",
        handle_error="Invalid edit plan handle",
        plan_expr="plan",
    ),
    "tx": Family(
        output="src/lua/lua_bindings_behavior_ops.generated.inc",
        handle_decl="nmo_lua_script_edit_tx_handle_data_t *handle = NULL;",
        handle_check="nmo_lua_behavior_check_active_edit_handle(state, 1, &handle)",
        handle_error="Invalid script edit handle",
        plan_expr="handle->plan",
    ),
}


@dataclass(frozen=True)
class Binding:
    family: str
    lua_function: str
    builder: str
    # One kind per builder parameter after the plan.
    args: tuple[str, ...]
    error: str


BINDINGS = [
    Binding("plan", "nmo_lua_plan_rename_io", "nmo_edit_plan_add_rename_io",
            ("id", "string"), "Failed to add rename io op"),
    Binding("plan", "nmo_lua_plan_remove_io", "nmo_edit_plan_add_remove_io",
            ("id", "bool"), "Failed to add remove io op"),
    Binding("plan", "nmo_lua_plan_remove_node", "nmo_edit_plan_add_remove_node",
            ("id", "id", "opt_u32"), "Failed to add remove node op"),
    Binding("plan", "nmo_lua_plan_add_behavior_link", "nmo_edit_plan_add_behavior_link",
            ("id", "id", "null", "id", "null", "opt_u32"),
            "Failed to add behavior link op"),
    Binding("plan", "nmo_lua_plan_rewire_behavior_link",
            "nmo_edit_plan_add_rewire_behavior_link",
            ("id", "id", "id"), "Failed to add rewire behavior link op"),
    Binding("plan", "nmo_lua_plan_set_behavior_link_delay",
            "nmo_edit_plan_add_set_behavior_link_delay",
            ("id", "u32"), "Failed to add behavior link delay op"),
    Binding("plan", "nmo_lua_plan_remove_behavior_link",
            "nmo_edit_plan_add_remove_behavior_link",
            ("id", "id"), "Failed to add remove behavior link op"),
    Binding("plan", "nmo_lua_plan_connect_parameter", "nmo_edit_plan_add_connect_parameter",
            ("id", "id", "null"), "Failed to add connect parameter op"),
    Binding("plan", "nmo_lua_plan_disconnect_parameter",
            "nmo_edit_plan_add_disconnect_parameter",
            ("id",), "Failed to add disconnect parameter op"),
    Binding("plan", "nmo_lua_plan_remove_parameter", "nmo_edit_plan_add_remove_parameter",
            ("id", "bool"), "Failed to add remove parameter op"),
    Binding("plan", "nmo_lua_plan_remove_operation", "nmo_edit_plan_add_remove_operation",
            ("id",), "Failed to add remove operation op"),
    Binding("tx", "nmo_lua_behavior_remove_node", "nmo_edit_plan_add_remove_node",
            ("id", "id", "flags"), "Failed to remove behavior node"),
    Binding("tx", "nmo_lua_behavior_rename_io", "nmo_edit_plan_add_rename_io",
            ("id", "string"), "Failed to rename io"),
    Binding("tx", "nmo_lua_behavior_remove_io", "nmo_edit_plan_add_remove_io",
            ("id", "bool"), "Failed to remove io"),
    Binding("tx", "nmo_lua_behavior_disconnect_parameter",
            "nmo_edit_plan_add_disconnect_parameter",
            ("id",), "Failed to disconnect parameter"),
    Binding("tx", "nmo_lua_behavior_remove_parameter", "nmo_edit_plan_add_remove_parameter",
            ("id", "bool"), "Failed to remove parameter"),
    Binding("tx", "nmo_lua_behavior_rewire_link", "nmo_edit_plan_add_rewire_behavior_link",
            ("id", "id", "id"), "Failed to rewire link"),
    Binding("tx", "nmo_lua_behavior_set_link_delay",
            "nmo_edit_plan_add_set_behavior_link_delay",
            ("id", "u32"), "Failed to set link delay"),
    Binding("tx", "nmo_lua_behavior_remove_link", "nmo_edit_plan_add_remove_behavior_link",
            ("id", "id"), "Failed to remove link"),
]


def read_prototypes() -> dict[str, list[tuple[str, str]]]:
    """Builder name -> [(C type, parameter name)], the plan parameter excluded."""
    text = HEADER.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    prototypes = {}
    for match in re.finditer(r"NMO_API\s+nmo_status_t\s+(nmo_edit_plan_add_\w+)\s*\(([^)]*)\)\s*;",
                             text):
        params = []
        for raw in match.group(2).split(","):
            raw = " ".join(raw.split())
            param = re.match(r"(.*?)\s*\b(\w+)$", raw)
            if not param:
                raise SystemExit(f"{GENERATOR}: cannot parse parameter '{raw}' of {match.group(1)}")
            params.append((param.group(1).replace(" *", " *").strip(), param.group(2)))
        if not params or params[0] != ("nmo_edit_plan_t *", "plan"):
            raise SystemExit(f"{GENERATOR}: {match.group(1)} does not take the plan first")
        prototypes[match.group(1)] = params[1:]
    return prototypes


def emit_binding(binding: Binding, family: Family,
                 params: list[tuple[str, str]]) -> str:
    if len(params) != len(binding.args):
        raise SystemExit(f"{GENERATOR}: {binding.lua_function} lists {len(binding.args)} "
                         f"arguments, {binding.builder} takes {len(params)}")
    reads = []
    call_args = [family.plan_expr]
    lua_index = 2
    for kind, (c_type, name) in zip(binding.args, params):
        expression, types = ARG_KINDS[kind]
        if types is None:
            if not c_type.endswith("*"):
                raise SystemExit(f"{GENERATOR}: {binding.lua_function} passes NULL for "
                                 f"{name} ({c_type})")
            call_args.append(expression)
            continue
        if c_type not in types:
            raise SystemExit(f"{GENERATOR}: {binding.lua_function} reads {name} as {kind}, "
                             f"but {binding.builder} takes {c_type}")
        declared = c_type if c_type.endswith("*") else c_type + " "
        reads.append(f"    {declared}{name} = {expression.format(index=lua_index)};")
        call_args.append(name)
        lua_index += 1
    call = f"{binding.builder}(\n        " + ",\n        ".join(call_args) + ");"
    return "\n".join([
        f"static int {binding.lua_function}(lua_State *state)",
        "{",
        f"    {family.handle_decl}",
        f"    nmo_status_t status = {family.handle_check};",
        "    if (status != NMO_OK) {",
        f'        return nmo_lua_raise_last_error(state, status, "{family.handle_error}");',
        "    }",
        "",
        *reads,
        "",
        f"    status = {call}",
        "    if (status != NMO_OK) {",
        f'        return nmo_lua_raise_last_error(state, status, "{binding.error}");',
        "    }",
        "    return 0;",
        "}",
    ])


def generate() -> dict[Path, str]:
    prototypes = read_prototypes()
    outputs = {}
    for family_name, family in FAMILIES.items():
        pieces = [
            f"/* Generated by {GENERATOR}; do not edit.\n"
            " * Lua wrappers of edit plan builders, included by the binding file. */",
        ]
        for binding in BINDINGS:
            if binding.family != family_name:
                continue
            if binding.builder not in prototypes:
                raise SystemExit(f"{GENERATOR}: {binding.builder} is not declared in "
                                 f"{HEADER.relative_to(ROOT)}")
            pieces.append(emit_binding(binding, family, prototypes[binding.builder]))
        outputs[ROOT / family.output] = "\n\n".join(pieces) + "\n"
    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true",
                        help="fail when a generated file differs from the specs")
    args = parser.parse_args()

    stale = []
    for path, content in generate().items():
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current == content:
            continue
        if args.check:
            stale.append(path.relative_to(ROOT).as_posix())
        else:
            path.write_text(content, encoding="utf-8", newline="\n")
            print(f"wrote {path.relative_to(ROOT).as_posix()}")
    if stale:
        print(f"{GENERATOR}: out of date: {', '.join(stale)}; run {GENERATOR}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
