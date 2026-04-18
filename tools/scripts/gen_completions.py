#!/usr/bin/env python3
"""Generate and verify shell completions for the nmo CLI.

The command hierarchy is parsed from the C registration tables so completions
do not drift from the actual dispatcher.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[2]
DISPATCH_C = ROOT / "tools" / "nmo_cli_dispatch.c"
BEHAVIOR_INTERFACE_C = ROOT / "tools" / "commands" / "nmo_cmd_behavior_interface.c"
COMPLETIONS_DIR = ROOT / "completions"
GENERATED_DIR = ROOT / "tools" / "generated"
COMPLETION_DATA_H = GENERATED_DIR / "nmo_completion_data.h"


GLOBAL_OPTIONS = [
    ("-h", "--help", "Show help", None),
    ("-V", "--version", "Show version", None),
    ("-f", "--format", "Output format", "text json json-pretty"),
    (None, "--color", "Color mode", "auto always never"),
    ("-o", "--output", "Write output to file", None),
    ("-v", "--verbose", "Increase verbosity", None),
    ("-q", "--quiet", "Suppress non-essential output", None),
    (None, "--no-pager", "Disable pager", None),
    (None, "--strict", "Enable strict validation mode", None),
    (None, "--fail-on-warning", "Exit with code 4 on warnings", None),
    (None, "--plugin", "Load extension plugin", None),
    ("-F", "--filter", "Filter objects by name pattern", None),
    (None, "--batch", "Process multiple files", None),
]


COMMON_ACTION_OPTIONS = [
    ("-c", "--class", "Filter by class", True),
    ("-n", "--name", "Filter by name", True),
    ("-i", "--index", "Select by index", True),
    ("-m", "--max-bytes", "Limit emitted bytes", True),
    ("-d", "--out-dir", "Output directory", True),
    (None, "--id", "Object ID", True),
    (None, "--top", "Show top N results", True),
    (None, "--sort", "Sort output", True),
    (None, "--reverse", "Reverse sort order", False),
    (None, "--dry-run", "Preview changes", False),
    (None, "--overwrite", "Overwrite existing files", False),
    (None, "--fix", "Show suggested fixes", False),
    (None, "--summary", "Summary only", False),
    (None, "--strip", "Strip matching data", False),
    (None, "--cascade", "Include dependents", False),
    (None, "--create", "Create missing objects", False),
    (None, "--dot", "Output DOT graph", False),
    (None, "--raw", "Show raw data", False),
    (None, "--all", "Process all matching entries", False),
    (None, "--replace", "Replace existing entry", True),
]


@dataclass
class Action:
    name: str
    alias: str | None
    brief: str
    sub_actions: list["Action"] = field(default_factory=list)
    default_sub: str | None = None


@dataclass
class Group:
    name: str
    alias: str | None
    brief: str
    actions_name: str
    actions: list[Action] = field(default_factory=list)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//.*?$", "", text, flags=re.MULTILINE)
    return text


def find_initializer_body(text: str, symbol: str) -> str:
    match = re.search(rf"\b{re.escape(symbol)}\s*\[\]\s*=\s*\{{", text)
    if not match:
        raise ValueError(f"Could not find initializer for {symbol}")
    open_brace = text.find("{", match.end() - 1)
    depth = 0
    in_string = False
    escape = False
    for i in range(open_brace, len(text)):
        ch = text[i]
        if in_string:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1 : i]
    raise ValueError(f"Unterminated initializer for {symbol}")


def split_entries(body: str) -> list[str]:
    entries: list[str] = []
    start: int | None = None
    depth = 0
    in_string = False
    escape = False
    for i, ch in enumerate(body):
        if in_string:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
            continue
        if ch == "{":
            if depth == 0:
                start = i + 1
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start is not None:
                entries.append(body[start:i].strip())
                start = None
    return entries


def split_fields(entry: str) -> list[str]:
    fields: list[str] = []
    start = 0
    depth = 0
    in_string = False
    escape = False
    for i, ch in enumerate(entry):
        if in_string:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
            continue
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            fields.append(entry[start:i].strip())
            start = i + 1
    fields.append(entry[start:].strip())
    return fields


def parse_c_string(value: str) -> str | None:
    value = value.strip()
    if value == "NULL":
        return None
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return bytes(value[1:-1], "utf-8").decode("unicode_escape")
    return value


def parse_action_array(source: str, symbol: str) -> list[Action]:
    body = find_initializer_body(source, symbol)
    actions: list[Action] = []
    for entry in split_entries(body):
        fields = split_fields(entry)
        if len(fields) < 3:
            continue
        name = parse_c_string(fields[0])
        if not name:
            continue
        alias = parse_c_string(fields[1])
        brief = parse_c_string(fields[2]) or ""
        sub_symbol = None
        default_sub = None
        if len(fields) >= 8:
            sub_candidate = fields[5].strip()
            if sub_candidate != "NULL":
                sub_symbol = sub_candidate
            default_sub = parse_c_string(fields[7])
        sub_actions: list[Action] = []
        if sub_symbol == "nmo_behavior_interface_sub_actions":
            iface_source = strip_comments(BEHAVIOR_INTERFACE_C.read_text(encoding="utf-8"))
            sub_actions = parse_action_array(iface_source, sub_symbol)
        actions.append(Action(name=name, alias=alias, brief=brief, sub_actions=sub_actions, default_sub=default_sub))
    return actions


def parse_groups() -> list[Group]:
    source = strip_comments(DISPATCH_C.read_text(encoding="utf-8"))
    body = find_initializer_body(source, "groups")
    groups: list[Group] = []
    for entry in split_entries(body):
        fields = split_fields(entry)
        if len(fields) < 4:
            continue
        name = parse_c_string(fields[0])
        if not name:
            continue
        alias = parse_c_string(fields[1])
        brief = parse_c_string(fields[2]) or ""
        actions_name = fields[3].strip()
        group = Group(name=name, alias=alias, brief=brief, actions_name=actions_name)
        group.actions = parse_action_array(source, actions_name)
        groups.append(group)
    return groups


def shell_quote(value: str) -> str:
    return "'" + value.replace("'", "'\\''") + "'"


def fish_condition_word(value: str) -> str:
    if re.fullmatch(r"[A-Za-z0-9_.:/+-]+", value):
        return value
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def ps_quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def zsh_desc(value: str) -> str:
    return value.replace("[", "\\[").replace("]", "\\]")


def all_group_names(groups: Iterable[Group]) -> list[str]:
    names: list[str] = []
    for group in groups:
        names.append(group.name)
        if group.alias:
            names.append(group.alias)
    return names


def action_names(action: Action) -> list[str]:
    names = [action.name]
    if action.alias:
        names.append(action.alias)
    return names


def render_header(shell: str) -> str:
    return (
        f"# {shell} completion for nmo - Virtools NMO file tool\n"
        "# Auto-generated by tools/scripts/gen_completions.py; do not edit by hand.\n\n"
    )


def render_bash(groups: list[Group]) -> str:
    lines: list[str] = [render_header("Bash")]
    group_words = " ".join(all_group_names(groups))
    flag_words = " ".join(
        item
        for short, long, _desc, _choices in GLOBAL_OPTIONS
        for item in (short, long)
        if item
    )
    lines += [
        "_nmo() {",
        "    local cur prev words cword",
        "    if declare -F _init_completion >/dev/null 2>&1; then",
        "        _init_completion || return",
        "    else",
        "        COMPREPLY=()",
        "        cur=${COMP_WORDS[COMP_CWORD]}",
        "        prev=${COMP_WORDS[COMP_CWORD-1]}",
        "        words=(\"${COMP_WORDS[@]}\")",
        "        cword=$COMP_CWORD",
        "    fi",
        f"    local groups=\"{group_words}\"",
        f"    local flags=\"{flag_words}\"",
        "    local value_options=\"-f --format --color -o --output --plugin -F --filter -c --class -n --name -i --index -m --max-bytes -d --out-dir --id --top --sort --replace\"",
        "",
        "    case \"$prev\" in",
        "        -f|--format) COMPREPLY=( $(compgen -W \"text json json-pretty\" -- \"$cur\") ); return ;;",
        "        --color) COMPREPLY=( $(compgen -W \"auto always never\" -- \"$cur\") ); return ;;",
        "        -o|--output|--plugin|-F|--filter|-d|--out-dir) compopt -o filenames 2>/dev/null; COMPREPLY=( $(compgen -f -- \"$cur\") ); return ;;",
        "        --*) if [[ \" $value_options \" == *\" $prev \"* ]]; then return; fi ;;",
        "        -*) if [[ \" $value_options \" == *\" $prev \"* ]]; then return; fi ;;",
        "    esac",
        "",
        "    _nmo_resolve_group() {",
        "        case \"$1\" in",
    ]
    for group in groups:
        if group.alias:
            lines.append(f"            {group.alias}) echo {shell_quote(group.name)} ;;")
    lines += [
        "            *) echo \"$1\" ;;",
        "        esac",
        "    }",
        "",
        "    _nmo_actions() {",
        "        case \"$1\" in",
    ]
    for group in groups:
        words = " ".join(name for action in group.actions for name in action_names(action))
        lines.append(f"            {group.name}) echo {shell_quote(words)} ;;")
    lines += [
        "            *) echo \"\" ;;",
        "        esac",
        "    }",
        "",
        "    _nmo_sub_actions() {",
        "        case \"$1/$2\" in",
    ]
    for group in groups:
        for action in group.actions:
            if action.sub_actions:
                words = " ".join(name for sub in action.sub_actions for name in action_names(sub))
                for action_name in action_names(action):
                    lines.append(f"            {group.name}/{action_name}) echo {shell_quote(words)} ;;")
    lines += [
        "            *) echo \"\" ;;",
        "        esac",
        "    }",
        "",
        "    local positional=()",
        "    local expect_value=0",
        "    local i word",
        "    for (( i=1; i < cword; i++ )); do",
        "        word=${words[$i]}",
        "        if (( expect_value )); then expect_value=0; continue; fi",
        "        if [[ \"$word\" == -* ]]; then",
        "            if [[ \" $value_options \" == *\" $word \"* ]]; then expect_value=1; fi",
        "            continue",
        "        fi",
        "        positional+=(\"$word\")",
        "    done",
        "",
        "    if [[ \"$cur\" == -* ]]; then COMPREPLY=( $(compgen -W \"$flags\" -- \"$cur\") ); return; fi",
        "    if (( ${#positional[@]} == 0 )); then COMPREPLY=( $(compgen -W \"$groups\" -- \"$cur\") ); return; fi",
        "    local group canonical actions subactions",
        "    group=${positional[0]}",
        "    canonical=$(_nmo_resolve_group \"$group\")",
        "    if (( ${#positional[@]} == 1 )); then",
        "        actions=$(_nmo_actions \"$canonical\")",
        "        COMPREPLY=( $(compgen -W \"$actions\" -- \"$cur\") )",
        "        return",
        "    fi",
        "    if (( ${#positional[@]} == 2 )); then",
        "        subactions=$(_nmo_sub_actions \"$canonical\" \"${positional[1]}\")",
        "        if [[ -n \"$subactions\" ]]; then",
        "            COMPREPLY=( $(compgen -W \"$subactions\" -- \"$cur\") )",
        "            return",
        "        fi",
        "    fi",
        "    local IFS=$'\\n'",
        "    COMPREPLY=( $(compgen -f -X '!*.@(nmo|cmo|vmo|json|obj)' -- \"$cur\") )",
        "    COMPREPLY+=( $(compgen -d -- \"$cur\") )",
        "    compopt -o filenames 2>/dev/null",
        "}",
        "",
        "complete -o default -F _nmo nmo",
        "",
    ]
    return "\n".join(lines)


def render_fish(groups: list[Group]) -> str:
    lines = [render_header("Fish"), "complete -c nmo -f", ""]
    lines += [
        "function __nmo_positionals",
        "    set -l cmd (commandline -opc)",
        "    set -l expect_value 0",
        "    for i in (seq 2 (count $cmd))",
        "        if test $expect_value -eq 1",
        "            set expect_value 0",
        "            continue",
        "        end",
        "        switch $cmd[$i]",
        "            case '-f' '--format' '--color' '-o' '--output' '--plugin' '-F' '--filter' '-c' '--class' '-n' '--name' '-i' '--index' '-m' '--max-bytes' '-d' '--out-dir' '--id' '--top' '--sort' '--replace'",
        "                set expect_value 1",
        "            case '-*'",
        "            case '*'",
        "                echo $cmd[$i]",
        "        end",
        "    end",
        "end",
        "function __nmo_pos_count",
        "    count (__nmo_positionals)",
        "end",
        "function __nmo_group",
        "    set -l p (__nmo_positionals)",
        "    test (count $p) -ge 1; and echo $p[1]",
        "end",
        "function __nmo_action",
        "    set -l p (__nmo_positionals)",
        "    test (count $p) -ge 2; and echo $p[2]",
        "end",
        "",
    ]
    for group in groups:
        lines.append(f"complete -c nmo -n 'test (__nmo_pos_count) -eq 0' -a {shell_quote(group.name)} -d {shell_quote(group.brief)}")
        if group.alias:
            lines.append(f"complete -c nmo -n 'test (__nmo_pos_count) -eq 0' -a {shell_quote(group.alias)} -d {shell_quote('Alias for ' + group.name)}")
    lines.append("")
    for group in groups:
        group_terms = [group.name] + ([group.alias] if group.alias else [])
        cond = " or ".join(f"test (__nmo_group) = {fish_condition_word(term)}" for term in group_terms)
        for action in group.actions:
            for name in action_names(action):
                desc = action.brief if name == action.name else f"Alias for {action.name}"
                lines.append(f"complete -c nmo -n 'test (__nmo_pos_count) -eq 1; and ({cond})' -a {shell_quote(name)} -d {shell_quote(desc)}")
            if action.sub_actions:
                action_terms = action_names(action)
                action_cond = " or ".join(f"test (__nmo_action) = {fish_condition_word(term)}" for term in action_terms)
                for sub in action.sub_actions:
                    for sub_name in action_names(sub):
                        desc = sub.brief if sub_name == sub.name else f"Alias for {sub.name}"
                        lines.append(f"complete -c nmo -n 'test (__nmo_pos_count) -eq 2; and ({cond}); and ({action_cond})' -a {shell_quote(sub_name)} -d {shell_quote(desc)}")
    lines.append("")
    for short, long, desc, choices in GLOBAL_OPTIONS:
        args = ["complete -c nmo"]
        if short:
            args.append(f"-s {short[1:]}")
        if long:
            args.append(f"-l {long[2:]}")
        if choices:
            args.append("-r")
            args.append(f"-a {shell_quote(choices)}")
        args.append(f"-d {shell_quote(desc)}")
        lines.append(" ".join(args))
    for short, long, desc, takes_value in COMMON_ACTION_OPTIONS:
        args = ["complete -c nmo"]
        if short:
            args.append(f"-s {short[1:]}")
        if long:
            args.append(f"-l {long[2:]}")
        if takes_value:
            args.append("-r")
        args.append(f"-d {shell_quote(desc)}")
        lines.append(" ".join(arg for arg in args if arg))
    lines += [
        "",
        "complete -c nmo -F -k -a '(for f in *.nmo *.cmo *.vmo *.json *.obj; test -f $f; and echo $f; end 2>/dev/null)'",
        "",
    ]
    return "\n".join(lines)


def render_powershell(groups: list[Group]) -> str:
    lines = [render_header("PowerShell")]
    lines.append("$_nmo_groups = @{")
    for group in groups:
        actions = ", ".join(ps_quote(name) for action in group.actions for name in action_names(action))
        lines.append(f"    {ps_quote(group.name)} = @({actions})")
    lines.append("}")
    lines.append("$_nmo_sub_actions = @{")
    for group in groups:
        for action in group.actions:
            if action.sub_actions:
                subs = ", ".join(ps_quote(name) for sub in action.sub_actions for name in action_names(sub))
                for action_name in action_names(action):
                    lines.append(f"    {ps_quote(group.name + '/' + action_name)} = @({subs})")
    lines.append("}")
    lines.append("$_nmo_aliases = @{")
    for group in groups:
        if group.alias:
            lines.append(f"    {ps_quote(group.alias)} = {ps_quote(group.name)}")
    lines.append("}")
    lines.append("$_nmo_group_descriptions = @{")
    for group in groups:
        lines.append(f"    {ps_quote(group.name)} = {ps_quote(group.brief)}")
    lines.append("}")
    flags = []
    for short, long, desc, choices in GLOBAL_OPTIONS:
        if short:
            flags.append((short, desc))
        if long:
            flags.append((long, desc))
    for short, long, desc, _takes_value in COMMON_ACTION_OPTIONS:
        if short:
            flags.append((short, desc))
        if long:
            flags.append((long, desc))
    lines.append("$_nmo_flags = @(")
    for name, desc in flags:
        lines.append(f"    @{{ Name = {ps_quote(name)}; Desc = {ps_quote(desc)} }}")
    lines.append(")")
    lines += [
        "",
        "Register-ArgumentCompleter -CommandName nmo -Native -ScriptBlock {",
        "    param($wordToComplete, $commandAst, $cursorPosition)",
        "    $tokens = $commandAst.ToString().Substring(0, $cursorPosition).Trim() -split '\\s+'",
        "    $valueOptions = @('-f','--format','--color','-o','--output','--plugin','-F','--filter','-c','--class','-n','--name','-i','--index','-m','--max-bytes','-d','--out-dir','--id','--top','--sort','--replace')",
        "    $positional = @()",
        "    $expectValue = $false",
        "    for ($i = 1; $i -lt $tokens.Count; $i++) {",
        "        if ($expectValue) { $expectValue = $false; continue }",
        "        if ($tokens[$i].StartsWith('-')) { if ($valueOptions -contains $tokens[$i]) { $expectValue = $true }; continue }",
        "        $positional += $tokens[$i]",
        "    }",
        "    if ($wordToComplete -ne '' -and $positional.Count -gt 0 -and $tokens[-1] -eq $wordToComplete) {",
        "        $positional = @($positional[0..($positional.Count - 2)])",
        "    }",
        "    if ($wordToComplete.StartsWith('-')) {",
        "        $_nmo_flags | Where-Object { $_.Name -like \"$wordToComplete*\" } | ForEach-Object { [System.Management.Automation.CompletionResult]::new($_.Name, $_.Name, 'ParameterValue', $_.Desc) }",
        "        return",
        "    }",
        "    if ($positional.Count -eq 0) {",
        "        $items = @()",
        "        foreach ($g in $_nmo_groups.Keys) { $items += @{ Name = $g; Desc = $_nmo_group_descriptions[$g] } }",
        "        foreach ($a in $_nmo_aliases.Keys) { $items += @{ Name = $a; Desc = \"Alias for $($_nmo_aliases[$a])\" } }",
        "        $items | Where-Object { $_.Name -like \"$wordToComplete*\" } | Sort-Object Name | ForEach-Object { [System.Management.Automation.CompletionResult]::new($_.Name, $_.Name, 'ParameterValue', $_.Desc) }",
        "        return",
        "    }",
        "    $group = $positional[0]",
        "    if ($_nmo_aliases.ContainsKey($group)) { $group = $_nmo_aliases[$group] }",
        "    if ($positional.Count -eq 1 -and $_nmo_groups.ContainsKey($group)) {",
        "        $_nmo_groups[$group] | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object { [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', \"$group $_\") }",
        "        return",
        "    }",
        "    if ($positional.Count -eq 2) {",
        "        $key = \"$group/$($positional[1])\"",
        "        if ($_nmo_sub_actions.ContainsKey($key)) {",
        "            $_nmo_sub_actions[$key] | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object { [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', \"$key $_\") }",
        "            return",
        "        }",
        "    }",
        "    $pattern = if ($wordToComplete) { \"$wordToComplete*\" } else { '*' }",
        "    Get-ChildItem -Path . -Filter $pattern -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.nmo', '.cmo', '.vmo', '.json', '.obj' } | ForEach-Object { [System.Management.Automation.CompletionResult]::new($_.Name, $_.Name, 'ProviderItem', $_.Name) }",
        "    $_nmo_flags | Where-Object { $_.Name -like \"$wordToComplete*\" } | ForEach-Object { [System.Management.Automation.CompletionResult]::new($_.Name, $_.Name, 'ParameterValue', $_.Desc) }",
        "}",
        "",
    ]
    return "\n".join(lines)


def render_zsh(groups: list[Group]) -> str:
    lines = ["#compdef nmo", render_header("Zsh").rstrip(), "local -a groups flags", "local state", "typeset -A opt_args", ""]
    lines.append("flags=(")
    for short, long, desc, choices in GLOBAL_OPTIONS:
        if short:
            lines.append(f"    '{short}[{zsh_desc(desc)}]'")
        if long:
            arg = f":value:(({choices.replace(' ', ' ') }))" if choices else ""
            lines.append(f"    '{long}[{zsh_desc(desc)}]{arg}'")
    for short, long, desc, takes_value in COMMON_ACTION_OPTIONS:
        if short:
            suffix = ":value:" if takes_value else ""
            lines.append(f"    '{short}[{zsh_desc(desc)}]{suffix}'")
        if long:
            suffix = ":value:" if takes_value else ""
            lines.append(f"    '{long}[{zsh_desc(desc)}]{suffix}'")
    lines.append(")")
    lines.append("groups=(")
    for group in groups:
        lines.append(f"    '{group.name}:{zsh_desc(group.brief)}'")
        if group.alias:
            lines.append(f"    '{group.alias}:Alias for {group.name}'")
    lines.append(")")
    lines += [
        "",
        "_nmo_resolve_group() {",
        "    case \"$1\" in",
    ]
    for group in groups:
        if group.alias:
            lines.append(f"        {group.alias}) echo {shell_quote(group.name)} ;;")
    lines += ["        *) echo \"$1\" ;;", "    esac", "}", ""]
    lines.append("_nmo_actions() {")
    lines.append("    local -a acts")
    lines.append("    case \"$1\" in")
    for group in groups:
        lines.append(f"        {group.name})")
        lines.append("            acts=(")
        for action in group.actions:
            lines.append(f"                '{action.name}:{zsh_desc(action.brief)}'")
            if action.alias:
                lines.append(f"                '{action.alias}:Alias for {action.name}'")
        lines.append("            ) ;;")
    lines += ["    esac", "    _describe -t actions 'action' acts", "}", ""]
    lines.append("_nmo_sub_actions() {")
    lines.append("    local -a acts")
    lines.append("    case \"$1/$2\" in")
    for group in groups:
        for action in group.actions:
            if action.sub_actions:
                for action_name in action_names(action):
                    lines.append(f"        {group.name}/{action_name})")
                    lines.append("            acts=(")
                    for sub in action.sub_actions:
                        lines.append(f"                '{sub.name}:{zsh_desc(sub.brief)}'")
                        if sub.alias:
                            lines.append(f"                '{sub.alias}:Alias for {sub.name}'")
                    lines.append("            ) ;;")
    lines += ["    esac", "    _describe -t sub-actions 'sub-action' acts", "}", ""]
    lines += [
        "_arguments -C $flags \\",
        "    '1:group:->group' \\",
        "    '2:action:->action' \\",
        "    '3:sub-action:->subaction' \\",
        "    '*:file:_files -g \"*.{nmo,cmo,vmo,json,obj}\"' && return",
        "",
        "case \"$state\" in",
        "    group)",
        "        _describe -t groups 'command group' groups",
        "        ;;",
        "    action)",
        "        local canonical",
        "        canonical=$(_nmo_resolve_group \"$line[1]\")",
        "        _nmo_actions \"$canonical\"",
        "        ;;",
        "    subaction)",
        "        local canonical",
        "        canonical=$(_nmo_resolve_group \"$line[1]\")",
        "        _nmo_sub_actions \"$canonical\" \"$line[2]\"",
        "        ;;",
        "esac",
        "",
    ]
    return "\n".join(lines)


def render_all(groups: list[Group]) -> dict[str, str]:
    return {
        "nmo.bash": render_bash(groups),
        "nmo.fish": render_fish(groups),
        "nmo.ps1": render_powershell(groups),
        "_nmo": render_zsh(groups),
    }


def c_string_literal(value: str) -> str:
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r", "\\r")
        .replace("\n", "\\n")
    )
    return f'"{escaped}"'


def render_c_header(rendered: dict[str, str]) -> str:
    shell_to_file = {
        "bash": "nmo.bash",
        "fish": "nmo.fish",
        "zsh": "_nmo",
        "powershell": "nmo.ps1",
    }
    lines: list[str] = [
        "/* Auto-generated by tools/scripts/gen_completions.py; do not edit by hand. */",
        "#ifndef NMO_COMPLETION_DATA_H",
        "#define NMO_COMPLETION_DATA_H",
        "",
        "#include <stddef.h>",
        "",
        "typedef struct nmo_completion_entry {",
        "    const char *shell;",
        "    const char *const *chunks;",
        "} nmo_completion_entry_t;",
        "",
    ]
    for shell, filename in shell_to_file.items():
        lines.append(f"static const char *const nmo_completion_{shell}[] = {{")
        for line in rendered[filename].splitlines(keepends=True):
            lines.append(f"    {c_string_literal(line)},")
        lines.append("    NULL,")
        lines.append("};")
        lines.append("")
    lines += [
        "static const nmo_completion_entry_t nmo_completion_entries[] = {",
        "    { \"bash\", nmo_completion_bash },",
        "    { \"fish\", nmo_completion_fish },",
        "    { \"zsh\", nmo_completion_zsh },",
        "    { \"powershell\", nmo_completion_powershell },",
        "    { \"ps1\", nmo_completion_powershell },",
        "};",
        "",
        "static const size_t nmo_completion_entry_count =",
        "    sizeof(nmo_completion_entries) / sizeof(nmo_completion_entries[0]);",
        "",
        "#endif /* NMO_COMPLETION_DATA_H */",
        "",
    ]
    return "\n".join(lines)


def check_completions(rendered: dict[str, str]) -> list[str]:
    errors: list[str] = []
    for filename, expected in rendered.items():
        path = COMPLETIONS_DIR / filename
        actual = path.read_text(encoding="utf-8") if path.exists() else ""
        if actual != expected:
            errors.append(f"{path.relative_to(ROOT)} is out of date; regenerate with tools/scripts/gen_completions.py --write")
    expected_header = render_c_header(rendered)
    actual_header = COMPLETION_DATA_H.read_text(encoding="utf-8") if COMPLETION_DATA_H.exists() else ""
    if actual_header != expected_header:
        errors.append(f"{COMPLETION_DATA_H.relative_to(ROOT)} is out of date; regenerate with tools/scripts/gen_completions.py --write")
    return errors


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="write generated completion files")
    parser.add_argument("--check", action="store_true", help="verify generated files match the repository")
    args = parser.parse_args(argv)

    if not args.write and not args.check:
        parser.error("pass --write or --check")

    groups = parse_groups()
    rendered = render_all(groups)

    if args.write:
        COMPLETIONS_DIR.mkdir(parents=True, exist_ok=True)
        for filename, content in rendered.items():
            (COMPLETIONS_DIR / filename).write_text(content, encoding="utf-8", newline="\n")
        GENERATED_DIR.mkdir(parents=True, exist_ok=True)
        COMPLETION_DATA_H.write_text(render_c_header(rendered), encoding="utf-8", newline="\n")

    if args.check:
        errors = check_completions(rendered)
        if errors:
            for error in errors:
                print(error, file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
