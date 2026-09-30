# CLI Reference

The `nmo` tool uses a group/action command structure:

```
nmo [global-options] <group> <action> [options] [file]
```

There are 23 groups. `nmo --help` lists them, `nmo <group> --help` lists a group's actions,
and `nmo <group> <action> --help` prints that action's usage. Most groups and actions also
have a short alias (for example `nmo obj ls`).

Global options: `-f`/`--format` (`text`, `json`, `json-pretty`), `-o`/`--output`,
`--color`, `-v`/`--verbose`, `-q`/`--quiet`, `--strict`. JSON output is available through the
format option, for example `-f json` or `-f json-pretty`.

Actions that save a modified file take `-o <output>`.

## File Inspection

```
nmo file info <file>          # Summary: object count, version, size
nmo file header <file>        # Raw file header fields
nmo file stats <file>         # File statistics
nmo file classes <file>       # Class ID distribution
nmo file plugins <file>       # Plugin dependencies
nmo file space [--top <N>] <file>  # Space usage analysis
```

## Object Discovery

```
nmo object list [--class <name>] <file>    # List objects, optionally filtered by class
nmo object show <id> <file>                # Detailed view of a single object
nmo object find [--name <pattern>] <file>  # Search objects by name glob
nmo object tree <file>                     # Object hierarchy
nmo object refs <id> <file>                # Object references
nmo object graph [options] <file>          # Reference graph export
nmo object list-fields <id> <file>         # Fields and values of one object
nmo object impact <id> <file>              # What deleting an object would affect
nmo object orphans [options] <file>        # Unreachable objects
nmo object cycles <file>                   # Circular references
```

## Chunk Inspection

```
nmo chunk list <file>          # List all chunks in the file
nmo chunk tree <file>          # Chunk hierarchy
nmo chunk show <id> <file>     # Inspect a single chunk in detail
nmo chunk find --class <name> <file>  # Find chunks by class
```

## Behavior Analysis

```
nmo behavior list <file>                    # List behaviors
nmo behavior stats <file>                   # Behavior statistics
nmo behavior graph <id> <file>              # Behavior graph structure
nmo behavior graph-boundary <id> <file>     # Boundary links and parameter crossings
nmo behavior show <id> <file>               # Behavior signature and parameters
nmo behavior dump [--all | <id>] <file>     # Behavior tree with decoded values
nmo behavior find [--name <pattern> | --op-type <type>] <file>  # Search behaviors
nmo behavior trace [--from <io>] <id> <file>  # Trace execution path from an IO
nmo behavior interface show <id> <file>     # Interface layout data
```

`behavior dump` takes `--values` to include decoded local and output parameter values, and
`--flows` (one behavior only, not with `--all`) to include execution and data flow summaries.

## Behavior Rewriting

```
nmo behavior replace-bb <id> --bb-guid <guid> <file> -o <out>   # Replace a leaf building block
nmo behavior fold-candidates --parent <id> <file>               # Report fold boundaries (read-only)
nmo behavior fold --parent <id> --nodes <ids> --bb-guid <guid> --name <name> --dry-run <file>
nmo behavior add-link --parent <beh-id> --from <io-id> --to <io-id> <file> -o <out>
nmo behavior remove-link <link-id> --parent <beh-id> <file> -o <out>
nmo patch apply <patch.json> [--dry-run]                        # Apply a rewrite patch
nmo patch apply --project <manifest.json> -o <out.cmo> [--dry-run]  # Generate a project manifest
nmo patch diff <patch.json>                                     # Preview a rewrite patch
```

`replace-bb` also accepts `--dry-run`. `fold` is analysis only for now: it reports the planned
fold without saving, and `--dry-run` is currently required.

## Script Editing

```
nmo script graph <id> <file>                # Export script edit graph
nmo script run [--dry-run] <script.lua> <file> -o <out>   # Run Lua automation script

nmo script node add --parent <id> --bb-guid <guid> [--name <name>] <file> -o <out>
nmo script node remove --parent <id> --node <id> <file> -o <out>

nmo script io add --behavior <id> --kind <input|output> --name <name> <file> -o <out>
nmo script io rename --io <id> --name <name> <file> -o <out>
nmo script io remove --io <id> <file> -o <out>

nmo script link add --parent <id> --from <io-id> --to <io-id> [--delay <n>] <file> -o <out>
nmo script link rewire --link <id> [--from <io-id>] [--to <io-id>] <file> -o <out>
nmo script link set-delay --link <id> --delay <n> <file> -o <out>
nmo script link remove --parent <id> --link <id> <file> -o <out>

nmo script param add --owner <id> --kind <in|out|local|shared> --type <type> --name <name> <file> -o <out>
nmo script param set --param <id> --value <typed-value> <file> -o <out>
nmo script param connect --from <param-id> --to <param-in-id> <file> -o <out>
nmo script param disconnect --to <param-in-id> <file> -o <out>
nmo script param remove --param <id> [--detach] <file> -o <out>

nmo script op add --parent <id> --op-guid <guid> [--in1 <id>] [--in2 <id>] [--out <id>] <file> -o <out>
nmo script op rewire --op <id> [--in1 <id>] [--in2 <id>] [--out <id>] <file> -o <out>
nmo script op remove --op <id> <file> -o <out>
```

## Parameters and Data Arrays

```
nmo parameter list <file>            # List parameters
nmo parameter show <id> <file>       # Parameter object with decoded value
nmo parameter dump [--all] [--type <guid>] <id> <file>  # Decoded value, owner, and type-specific fields
nmo parameter set <param-id> <value> <file> -o <out>   # Set a parameter value

nmo data list <file>                 # List data arrays
nmo data show <id> <file>            # Data array schema
nmo data dump <id> <file>            # Data array contents
nmo data set-cell <id> --row <r> --col <c> --value <val> <file> -o <out>  # Modify a single cell
```

## Scene and Entity

```
nmo scene list <file>          # List scenes and levels
nmo scene show <id> <file>     # Scene details
nmo scene set <id> [options] <file> -o <out>   # Set scene properties

nmo entity list <file>         # List 3D entities
nmo entity show <id> <file>    # Entity details and transform
nmo entity set-position <id> <x> <y> <z> <file> -o <out>
nmo entity set-parent | set-camera | set-light ...   # See `nmo entity <action> --help`
```

## Mesh, Texture, Material, Animation

```
nmo mesh list <file>
nmo mesh show <id> <file>
nmo mesh export --id <id> --out-dir meshes <file>   # Export as OBJ
nmo mesh import <obj-file> <nmo-file> -o <out> [--replace <id> | --replace-name <name>] [--dry-run]

nmo texture list <file>
nmo texture show <id> <file>
nmo texture extract --id <id> --out-dir textures <file>
nmo texture replace <id> --file <image> <nmo-file> -o <out> [--dry-run]   # Replace bitmap data

nmo material list <file>
nmo material show <id> <file>
nmo material set <id> [options] <file> -o <out>

nmo animation list <file>
nmo animation show <id> <file>
nmo animation keys <id> <file>                      # Decoded key data
nmo animation export --id <id> --out-dir anims <file>   # Export as JSON
nmo animation import <json-file> <nmo-file> -o <out> [--replace <id> | --replace-name <name>] [--dry-run]
```

## Resources and Extensions

```
nmo resource list <file>             # Embedded resources
nmo resource show [--index <n> | --name <name>] <file>
nmo resource extract --out-dir <dir> [--index <n> | --name <name>] [--overwrite] <file>
nmo resource info [--index <n> | --name <name>] <file>   # Detect resource format
nmo resource import | replace | remove ...               # See `nmo resource <action> --help`

nmo extension list                   # Registered extensions
nmo extension info <file>            # Extension metadata
nmo extension check <file>           # Check plugin dependencies of a file
nmo extension load <path>            # Load an extension DLL (not available in the REPL)
```

## Type System

```
nmo type list                   # List all registered types
nmo type show <name>            # Type details and fields
nmo type class-tree             # CK class inheritance hierarchy
```

## Validation

```
nmo validate all <file>              # Run all validation checks
nmo validate structure [--fix] <file>  # File structure
nmo validate references <file>       # Check reference integrity
nmo validate resources <file>        # Embedded resources
nmo validate orphans <file>          # Unreferenced objects
```

There is no signature or checksum check. The file signature is verified whenever a file is
loaded, but the stored CRC is not compared against the contents.

## Editing

```
nmo object rename <id> <new_name> <file> -o <out>
nmo object delete <id> <file> -o <out>
nmo object create --class <name> [--name <name>] <file> -o <out>
nmo object copy <id>[,<id>,...] <file> -o <out>
nmo object set-field <id> <field> <value> <file> -o <out>
nmo -f json object export --id <id> <file>               # Importable semantic snapshot
nmo object import -f json <snapshot.json> <file> -o <out>  # Import object snapshot JSON
nmo texture extract --id <id> --out-dir textures <file>
```

`object export` JSON is a semantic snapshot protocol intended for round-trip
with `object import -f json`. Snapshot fields use `name`, `kind`, `type_guid`,
and `value`; arrays carry full `items` and `count` data. Legacy flat field maps
and preview-only `{name,value_str}` exports are not accepted by import.

## Convert

```
nmo convert copy -o <out> [options] <file>       # Round-trip copy with save options
nmo convert version [--fast-save] <file>         # Print file version metadata
nmo convert version [--fast-save] -o <out> <file>  # Same as copy; does not change the format version
nmo convert strip -o <out> [options] <file>      # Remove objects by class/name pattern
nmo convert merge [--fast-save] -o <out> <source> <target>  # Merge objects from source into target
nmo convert export [options] <file> -o <out>     # Export selected objects to a new file
```

## Diff

```
nmo diff summary <file-a> <file-b>    # High-level comparison
nmo diff objects <file-a> <file-b>    # Topology-aware object diff, git-style unified output
nmo diff chunks <file-a> <file-b>     # Chunk-level comparison
nmo diff full <file-a> <file-b>       # Full comparison
```

## Debugging and REPL

```
nmo debug load-phases [--profile=full|metadata|header-only] <file>
                                # Load pipeline phase details and statistics
nmo debug chunks <file>         # Chunk parse details
nmo debug objects <file>        # Object load details
nmo debug export <file>         # JSON snapshot for debugging
nmo debug probe <kind> --behavior <id> <file> -o <out>   # Inject diagnostic script probes
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
