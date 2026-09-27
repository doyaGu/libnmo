# CLI Reference

The `nmo` tool uses a group/action command structure. JSON output is available
through the global format option, for example `-f json` or `-f json-pretty`.

## File Inspection

```
nmo file info <file>          # Summary: object count, version, size
nmo file header <file>        # Raw file header fields
```

## Object Discovery

```
nmo object list [--class <name>] <file>    # List objects, optionally filtered by class
nmo object show <id> <file>                # Detailed view of a single object
nmo object find [--name <pattern>] <file>  # Search objects by name glob
```

## Chunk Inspection

```
nmo chunk list <file>          # List all chunks in the file
nmo chunk show <id> <file>     # Inspect a single chunk in detail
```

## Behavior Analysis

```
nmo behavior graph <id> <file>              # Behavior graph structure
nmo behavior show <id> <file>               # Behavior details and parameters
nmo behavior find [--name <pattern> | --op-type <type>] <file>  # Search behaviors
nmo behavior trace --from <io> <id> <file>  # Trace execution paths
nmo behavior interface show <id> <file>     # Interface layout data
```

## Script Editing

```
nmo script graph <id> <file>                # Export script edit graph
nmo script run <script.lua> <file> -o <out> # Run Lua automation script
nmo script node <id> <file> -o <out>        # Script node editing
nmo script io <id> <file> -o <out>          # Script IO editing
```

## Scene and Entity

```
nmo scene list <file>          # List scenes and levels
nmo scene show <id> <file>     # Scene details

nmo entity list <file>         # List 3D entities
nmo entity show <id> <file>    # Entity details and transform
```

## Mesh, Texture, Material, Animation

```
nmo mesh list <file>
nmo mesh show <id> <file>
nmo mesh export --id <id> --out-dir meshes <file>

nmo texture list <file>
nmo texture extract --id <id> --out-dir textures <file>

nmo material list <file>
nmo material show <id> <file>

nmo animation list <file>
nmo animation export --id <id> --out-dir anims <file>
```

## Type System

```
nmo type list                   # List all registered types
nmo type show <name>            # Type details and fields
nmo type class-tree             # CK class inheritance hierarchy
```

## Validation

```
nmo validate all <file>         # Run all validation checks
nmo validate references <file>  # Check reference integrity
```

## Editing

```
nmo object rename <id> "NewName" <file> -o <out>
nmo object delete <id> <file> -o <out>
nmo -f json object export --id <id> <file>               # Importable semantic snapshot
nmo object import -f json <snapshot.json> <file> -o <out>  # Import object snapshot JSON
nmo texture extract --id <id> --out-dir textures <file>
nmo convert copy <file> -o <out>  # Round-trip copy / format conversion
```

`object export` JSON is a semantic snapshot protocol intended for round-trip
with `object import -f json`. Snapshot fields use `name`, `kind`, `type_guid`,
and `value`; arrays carry full `items` and `count` data. Legacy flat field maps
and preview-only `{name,value_str}` exports are not accepted by import.

## Diff and Patch

```
nmo diff objects <file-a> <file-b>    # Compare two files at the object level
nmo patch apply <patch.json> <file> -o <out>  # Apply a patch file
```

## Debugging and REPL

```
nmo debug load-phases <file>    # Show 15-phase load pipeline timing
nmo repl start <file>           # Interactive REPL with tab completion
```

The REPL has two layers:

- Legacy browsing shortcuts such as `list`, `show`, `dump`, `param`, `refs`,
  `trace`, `query`, `eval`, `stats`, `meta`, `verify`, and `export` remain
  optimized for interactive exploration.
- CLI-shaped grouped commands such as `object show`, `object graph`,
  `parameter dump`, `behavior interface`, `resource extract`, `mesh export`,
  and `validate all` reuse the same command registry and family command cores
  as the CLI, but operate on the currently loaded in-memory session.

REPL grouped read commands do not accept an implicit current-file operand. Use
the session already loaded in the REPL; `diff` is the exception, where the REPL
session is the left side and the explicit file operand is the comparison side.
Commands that write external artifacts, such as `resource extract`,
`texture extract`, `mesh export`, `animation export`, and `debug export`, still
require their explicit output path or directory and do not mark the session
dirty.

Use `cli ...` inside the REPL when global CLI options are needed:

```text
cli -f json object list --top 5
cli -o debug.json debug export
cli --strict validate all
```

Supported REPL grouped mutations are limited to `object rename`,
`object delete`, `object create`, `object copy`, and `parameter set`. They mutate
the loaded session and must be persisted with `save <path>`. Other file-writing,
import, replace, convert, and fix-style CLI actions are rejected in the REPL
grouped command path.

## Shell Completions

Completions are provided in `completions/` for Bash, Fish, Zsh, and PowerShell.
They can also be emitted by the CLI:

```sh
nmo completion bash
nmo completion fish
nmo completion zsh
nmo completion powershell
```
