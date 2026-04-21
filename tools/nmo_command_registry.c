/**
 * @file nmo_command_registry.c
 * @brief Shared CLI/REPL command registry metadata.
 */

#include "nmo_command_registry.h"
#include "nmo_cli_common.h"
#include "nmo_cmd_ctx.h"
#include "nmo_tool_common.h"

#include "commands/nmo_cmd_file.h"
#include "commands/nmo_cmd_chunk.h"
#include "commands/nmo_cmd_object.h"
#include "commands/nmo_cmd_debug.h"
#include "commands/nmo_cmd_type.h"
#include "commands/nmo_cmd_validate.h"
#include "commands/nmo_cmd_resource.h"
#include "commands/nmo_cmd_behavior.h"
#include "commands/nmo_cmd_patch.h"
#include "commands/nmo_cmd_parameter.h"
#include "commands/nmo_cmd_script.h"
#include "commands/nmo_cmd_convert.h"
#include "commands/nmo_cmd_diff.h"
#include "commands/nmo_cmd_query.h"
#include "commands/nmo_cmd_extension.h"
#include "commands/nmo_cmd_texture.h"
#include "commands/nmo_cmd_data.h"
#include "commands/nmo_cmd_scene.h"
#include "commands/nmo_cmd_entity.h"
#include "commands/nmo_cmd_material.h"
#include "commands/nmo_cmd_animation.h"
#include "commands/nmo_cmd_mesh.h"
#include "commands/nmo_cmd_completion.h"

#include <stdio.h>
#include <string.h>
/* Usage printers for implemented commands */
static void file_info_usage(FILE *out) {
    fprintf(out, "Usage: nmo file info <file>\n\n");
    fprintf(out, "Show basic file information including object count, manager count,\n");
    fprintf(out, "and CK version.\n");
}

static void file_header_usage(FILE *out) {
    fprintf(out, "Usage: nmo file header <file>\n\n");
    fprintf(out, "Show detailed file header fields including signature, version,\n");
    fprintf(out, "CRC, packed/unpacked sizes, etc.\n");
}

static void file_stats_usage(FILE *out) {
    fprintf(out, "Usage: nmo file stats <file>\n\n");
    fprintf(out, "Show file statistics including object counts by category,\n");
    fprintf(out, "chunk statistics, and unique class counts.\n");
}

static void file_classes_usage(FILE *out) {
    fprintf(out, "Usage: nmo file classes <file>\n\n");
    fprintf(out, "Show class ID distribution with counts and type names.\n");
}

static void file_plugins_usage(FILE *out) {
    fprintf(out, "Usage: nmo file plugins <file>\n\n");
    fprintf(out, "Show plugin dependencies and their status.\n");
}

static void file_space_usage(FILE *out) {
    fprintf(out, "Usage: nmo file space [--top <N>] <file>\n\n");
    fprintf(out, "Analyze space usage: per-class breakdown with cumulative %%,\n");
    fprintf(out, "per-object compression ratios, and top N largest objects.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --top <N>    Show top N objects by size (default: 15)\n");
}

static void chunk_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo chunk list <file>\n\n");
    fprintf(out, "List all chunks in the file (including sub-chunks) with a stable\n");
    fprintf(out, "flat index, parent index, owner object ID, class ID, and data size.\n");
}

static void chunk_tree_usage(FILE *out) {
    fprintf(out, "Usage: nmo chunk tree <file>\n\n");
    fprintf(out, "Show chunk sub-chunk hierarchy for each object.\n");
}

static void chunk_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo chunk show --index <n> <file>\n");
    fprintf(out, "       nmo chunk show <object-id> <file>\n\n");
    fprintf(out, "Show detailed information about a specific chunk.\n");
    fprintf(out, "--index selects from the flat list shown by 'nmo chunk list'.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  --hexdump           Include hexdump -C compatible output (text) or data_hex (JSON)\n");
    fprintf(out, "  --max-bytes, -m <n> Limit data bytes emitted for --hexdump (default 256; 0 = all)\n");
}

static void chunk_find_usage(FILE *out) {
    fprintf(out, "Usage: nmo chunk find --class <name> <file>\n\n");
    fprintf(out, "Find chunks by class (includes derived classes).\n");
}

static void object_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo object list [--class <name>] [--filter <expr>] <file>\n\n");
    fprintf(out, "List all objects in the file.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --class, -c <name>    Filter by class (includes derived classes)\n");
    fprintf(out, "  --filter, -f <expr>   Filter by DSL expression (truthy = include)\n");
}

static void object_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo object show [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show detailed information about a specific object.\n");
}

static void object_tree_usage(FILE *out) {
    fprintf(out, "Usage: nmo object tree <file>\n\n");
    fprintf(out, "Show the object parent/child hierarchy as a tree.\n");
}

static void object_find_usage(FILE *out) {
    fprintf(out, "Usage: nmo object find [--name <pattern>] [--class <name>] <file>\n\n");
    fprintf(out, "Find objects by name pattern and/or class (includes derived classes).\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --name <pattern>      Name filter (case-insensitive). Patterns: *, prefix*, *suffix, exact\n");
    fprintf(out, "  --class, -c <name>    Class filter (includes derived classes)\n\n");
    fprintf(out, "Note: in PowerShell, quote '*' like --name '*' to avoid wildcard expansion.\n");
}

static void object_refs_usage(FILE *out) {
    fprintf(out, "Usage: nmo object refs [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show incoming and outgoing references for a specific object.\n");
}

static void object_rename_usage(FILE *out) {
    fprintf(out, "Usage: nmo object rename <id> <new_name> <file> -o <output>\n\n");
    fprintf(out, "Rename an object and save to a new file.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required)\n");
}

static void object_export_usage(FILE *out) {
    fprintf(out, "Usage: nmo object export [options] <file>\n\n");
    fprintf(out, "Export object data. JSON output is an importable semantic snapshot.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --class, -c <name>   Filter by class (includes derived classes)\n");
    fprintf(out, "  --name, -n <pat>     Filter by name pattern\n");
    fprintf(out, "  --filter, -f <expr>  Filter by DSL expression\n");
    fprintf(out, "  --depth, -d <n>      Recursion depth (default: 4)\n");
    fprintf(out, "  --full               Full detail mode for text output (depth 8)\n");
    fprintf(out, "  --id <n>             Export specific object by ID\n");
}

static void object_impact_usage(FILE *out) {
    fprintf(out, "Usage: nmo object impact [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show what would be affected if the specified object were deleted.\n");
    fprintf(out, "Reports direct dependents (incoming references) and the full\n");
    fprintf(out, "cascade deletion set.\n");
}

static void object_orphans_usage(FILE *out) {
    fprintf(out, "Usage: nmo object orphans [options] <file>\n\n");
    fprintf(out, "Find objects not reachable from any root (CKLevel, CKScene, etc).\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --class, -c <name>   Filter by class (includes derived classes)\n");
}

static void object_cycles_usage(FILE *out) {
    fprintf(out, "Usage: nmo object cycles <file>\n\n");
    fprintf(out, "Detect circular references in the object graph using DFS.\n");
}

static void object_graph_usage(FILE *out) {
    fprintf(out, "Usage: nmo object graph [options] <file>\n\n");
    fprintf(out, "Export the full reference graph as text summary, DOT digraph,\n");
    fprintf(out, "or JSON edge list.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --dot              Output DOT digraph format\n");
    fprintf(out, "  --kind <name>      Filter edges by ref kind (case-insensitive)\n");
}

static void object_delete_usage(FILE *out) {
    fprintf(out, "Usage: nmo object delete [options] <id>[,<id>,...] <file> -o <output>\n\n");
    fprintf(out, "Delete objects from a file.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <file>  Output file (required unless --dry-run)\n");
    fprintf(out, "  -c, --class <name>   Filter by class (includes derived)\n");
    fprintf(out, "  -n, --name <pat>     Filter by name wildcard pattern\n");
    fprintf(out, "  -f, --filter <expr>  Filter by DSL expression\n");
    fprintf(out, "  --cascade            Delete dependents (default: safe-detach)\n");
    fprintf(out, "  --dry-run            Preview only, do not save\n");
    fprintf(out, "  --strict             Fail if any ID not found\n");
}

static void object_create_usage(FILE *out) {
    fprintf(out, "Usage: nmo object create --class <name> [--name <name>] <file> -o <output>\n\n");
    fprintf(out, "Create a new object in the file.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  -c, --class <name>     Class name (required)\n");
    fprintf(out, "  -n, --name <name>      Object name\n");
    fprintf(out, "  --type-guid <guid>     Type GUID (d1,d2 format)\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

static void object_copy_usage(FILE *out) {
    fprintf(out, "Usage: nmo object copy [options] <id>[,<id>,...] <file> -o <output>\n\n");
    fprintf(out, "Copy objects within a file.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <file>  Output file (required unless --dry-run)\n");
    fprintf(out, "  -c, --class <name>   Filter by class (includes derived)\n");
    fprintf(out, "  -n, --name <pat>     Filter by name wildcard pattern\n");
    fprintf(out, "  -f, --filter <expr>  Filter by DSL expression\n");
    fprintf(out, "  --cascade            Copy dependents\n");
    fprintf(out, "  --dry-run            Preview without saving\n");
}

static void object_import_usage(FILE *out) {
    fprintf(out, "Usage: nmo object import -f json <json-file> <nmo-file> -o <output>\n\n");
    fprintf(out, "Import object export snapshot JSON.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -f, --format json    Input format (required)\n");
    fprintf(out, "  -o, --output <file>  Output file (required unless --dry-run)\n");
    fprintf(out, "  --create             Create objects not found by ID\n");
    fprintf(out, "  --dry-run            Preview changes without saving\n");
}

static void object_set_field_usage(FILE *out) {
    fprintf(out, "Usage: nmo object set-field [--id <id> | --name <name> | <id>] <field> <value> <file> -o <output>\n\n");
    fprintf(out, "Set an object's typed field and save. Use 'object list-fields' to see field names.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>  Output file (required unless --dry-run)\n");
    fprintf(out, "  --dry-run            Preview without saving\n");
}

static void object_list_fields_usage(FILE *out) {
    fprintf(out, "Usage: nmo object list-fields [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "List all typed fields of an object with current values.\n");
}

static void debug_diagnostic_note(FILE *out) {
    fprintf(out, "\nNotes:\n");
    fprintf(out, "  Diagnostic output is for inspection and is not a stable API.\n");
}

static void debug_load_phases_usage(FILE *out) {
    fprintf(out, "Usage: nmo debug load-phases [--profile=full|metadata|header-only] <file>\n\n");
    fprintf(out, "Show load pipeline phase details including reference resolution\n");
    fprintf(out, "statistics and index memory usage when a full load is used.\n");
    debug_diagnostic_note(out);
}

static void debug_chunks_usage(FILE *out) {
    fprintf(out, "Usage: nmo debug chunks <file>\n\n");
    fprintf(out, "Show detailed chunk parse information for debugging.\n");
    debug_diagnostic_note(out);
}

static void debug_objects_usage(FILE *out) {
    fprintf(out, "Usage: nmo debug objects <file>\n\n");
    fprintf(out, "Show detailed object load information for debugging.\n");
    debug_diagnostic_note(out);
}

static void repl_start_usage(FILE *out) {
    fprintf(out, "Usage: nmo repl start <file>\n\n");
    fprintf(out, "Start the interactive debugger REPL for exploring the file.\n");
}

static void type_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo type list\n\n");
    fprintf(out, "List all registered class types.\n");
}

static void type_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo type show <class-name-or-id>\n\n");
    fprintf(out, "Show details about a specific class type.\n");
}

static void validate_all_usage(FILE *out) {
    fprintf(out, "Usage: nmo validate all <file>\n\n");
    fprintf(out, "Run all validation checks on the file.\n\n");
    fprintf(out, "Exit codes:\n");
    fprintf(out, "  0 - Success (valid file)\n");
    fprintf(out, "  3 - Strict mode failure (with --strict)\n");
    fprintf(out, "  4 - Warnings exist (with --fail-on-warning)\n");
}

static void validate_structure_usage(FILE *out) {
    fprintf(out, "Usage: nmo validate structure [--fix] <file>\n\n");
    fprintf(out, "Validate basic file/chunk structure and report issues.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --fix               Show suggested fixes for each issue\n");
    fprintf(out, "  --strict            Exit with code 3 if errors exist\n");
    fprintf(out, "  --fail-on-warning   Exit with code 4 if warnings exist\n");
}

static void validate_references_usage(FILE *out) {
    fprintf(out, "Usage: nmo validate references [--fix] <file>\n\n");
    fprintf(out, "Validate object reference graph (broken/self references, etc).\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --fix               Show suggested fixes for each issue\n");
    fprintf(out, "  --strict            Exit with code 3 if errors exist\n");
    fprintf(out, "  --fail-on-warning   Exit with code 4 if warnings exist\n");
}

static void validate_resources_usage(FILE *out) {
    fprintf(out, "Usage: nmo validate resources <file>\n\n");
    fprintf(out, "Validate embedded resource/plugin entries against the registry.\n\n");
    fprintf(out, "Global options:\n");
    fprintf(out, "  --strict            Exit with code 3 if errors exist\n");
    fprintf(out, "  --fail-on-warning   Exit with code 4 if warnings exist\n");
}

static void validate_orphans_usage(FILE *out) {
    fprintf(out, "Usage: nmo validate orphans [options] <file>\n\n");
    fprintf(out, "Find unreferenced (orphan) objects with zero incoming references.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --class, -c <name>  Filter by class (includes derived classes)\n");
    fprintf(out, "  --strict            Exit with code 3 if orphans found\n");
    fprintf(out, "  --summary           Summary only (no per-object listing)\n");
    fprintf(out, "  --strip -o <file>   Remove orphans and save cleaned file\n");
}

static void debug_export_usage(FILE *out) {
    fprintf(out, "Usage: nmo debug export [--data] [--max-bytes <n>] <file>\n\n");
    fprintf(out, "Export a JSON snapshot for debugging (object list + chunk metadata).\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --data, --include-data     Include chunk raw bytes as data_hex\n");
    fprintf(out, "  --max-bytes <n>            Limit emitted chunk bytes when --data is set (default 4096)\n");
    debug_diagnostic_note(out);
    fprintf(out, "  Use --format json-pretty for human-readable JSON.\n");
    fprintf(out, "  Use -o/--output to write JSON to a file.\n");
}

static void resource_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo resource list <file>\n\n");
    fprintf(out, "List included files/resources embedded in the NMO.\n");
}

static void resource_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo resource show [--index <n> | --name <name>] <file>\n\n");
    fprintf(out, "Show details for a single included resource, including owner object IDs.\n");
}

static void resource_extract_usage(FILE *out) {
    fprintf(out, "Usage: nmo resource extract --out-dir <dir> [--index <n> | --name <name>] [--overwrite] <file>\n\n");
    fprintf(out, "Extract included resources to a directory.\n");
    fprintf(out, "Entries marked metadata-only or with no payload are skipped.\n");
}

static void resource_import_usage(FILE *out) {
    fprintf(out, "Usage: nmo resource import -o <output> [--name <name>] [--owner <ids>] <disk-file> <nmo-file>\n\n");
    fprintf(out, "Import a file from disk as a new included resource.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required)\n");
    fprintf(out, "  -n, --name <name>      Resource name (default: basename of disk file)\n");
    fprintf(out, "  --owner <ids>          Owner object IDs (comma-separated)\n");
}

static void resource_replace_usage(FILE *out) {
    fprintf(out, "Usage: nmo resource replace -o <output> [--dry-run] [--index <n> | --name <name>] <disk-file> <nmo-file>\n\n");
    fprintf(out, "Replace an included resource payload, such as an embedded file entry.\n");
    fprintf(out, "This does not update CKTexture bitmap data; use `texture replace` for texture objects.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  -i, --index <n>        Resource index\n");
    fprintf(out, "  -n, --name <name>      Resource name\n");
    fprintf(out, "  --dry-run              Preview only, do not save\n");
}

static void resource_remove_usage(FILE *out) {
    fprintf(out, "Usage: nmo resource remove -o <output> [--index <n> | --name <name>] [--dry-run] <nmo-file>\n\n");
    fprintf(out, "Remove an included file from the NMO.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  -i, --index <n>        Resource index\n");
    fprintf(out, "  -n, --name <name>      Resource name\n");
    fprintf(out, "  --dry-run              Preview only, do not save\n");
}

static void resource_info_usage(FILE *out) {
    fprintf(out, "Usage: nmo resource info [--index <n> | --name <name>] <file>\n\n");
    fprintf(out, "Detect format and show metadata for a resource.\n");
    fprintf(out, "If --index or --name is given, reads from NMO included files.\n");
    fprintf(out, "Otherwise reads the positional file from disk.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -i, --index <n>        Resource index (in NMO)\n");
    fprintf(out, "  -n, --name <name>      Resource name (in NMO)\n");
}

static void behavior_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior list <file>\n\n");
    fprintf(out, "List behavior objects in the file (CKBehavior-derived).\n");
    fprintf(out, "\nOutput:\n");
    fprintf(out, "  Use global -f json or -f json-pretty for machine-readable output.\n");
}

static void behavior_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior show [options] [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show behavior signature: IO ports, parameters with types,\n");
    fprintf(out, "sub-behaviors, and links.\n\n");
    fprintf(out, "Output:\n");
    fprintf(out, "  Use global -f json or -f json-pretty for machine-readable output.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --raw    Show raw reflection output (like object show)\n");
}

static void behavior_stats_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior stats <file>\n\n");
    fprintf(out, "Show behavior counts and distribution by class.\n");
    fprintf(out, "\nOutput:\n");
    fprintf(out, "  Use global -f json or -f json-pretty for machine-readable output.\n");
}

static void behavior_graph_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior graph [options] [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Export a behavior graph with parameter and operation links.\n\n");
    fprintf(out, "Output:\n");
    fprintf(out, "  Use global -f json or -f json-pretty for machine-readable output.\n");
    fprintf(out, "  --dot emits Graphviz DOT and is text format only.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --dot               Include DOT graph output (text format only)\n");
    fprintf(out, "  --max-nodes <n>      Limit node output (0 = no limit)\n");
    fprintf(out, "  --max-edges <n>      Limit edge output (0 = no limit)\n");
}

static void script_graph_usage(FILE *out) {
    fprintf(out,
            "Usage: nmo script graph [--depth N] [--dot] [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out,
            "Build the script edit graph IR for a script root and report\n");
    fprintf(out,
            "control/data edges together with edit-readiness diagnostics.\n\n");
    fprintf(out,
            "Output:\n");
    fprintf(out,
            "  Use global -f json or -f json-pretty for machine-readable output.\n");
}

static void behavior_graph_boundary_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior graph-boundary [options] [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Export behavior graph boundary links and parameter crossings.\n\n");
    fprintf(out, "Output:\n");
    fprintf(out, "  Use global -f json or -f json-pretty for machine-readable output.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --depth <n>      Recursion depth (default: unlimited)\n");
}

static void behavior_replace_bb_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior replace-bb <behavior-id> --guid <guid> [options] <file> -o <output>\n\n");
    fprintf(out, "Replace an existing leaf building-block behavior while preserving graph boundaries.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --guid <guid>          Replacement building-block GUID (required)\n");
    fprintf(out, "  --name <name>          Replacement behavior name\n");
    fprintf(out, "  --version <n>          Replacement building-block version (default: 65536)\n");
    fprintf(out, "  --preserve-links       Require unchanged control boundary links\n");
    fprintf(out, "  --preserve-params      Require unchanged parameter boundary edges\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

static void behavior_fold_candidates_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior fold-candidates --parent <id> [options] <file>\n\n");
    fprintf(out, "Report read-only subgraph fold candidate boundaries.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -p, --parent <id>  Parent behavior ID\n");
    fprintf(out, "  -d, --depth <n>    Recursion depth (default: unlimited)\n");
}

static void behavior_fold_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior fold --parent <id> --nodes <ids> --guid <guid> --name <name> [options] <file> -o <output>\n\n");
    fprintf(out, "Analyze a graph/script subgraph fold into a high-level building block.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -p, --parent <id>       Parent behavior ID\n");
    fprintf(out, "  --nodes <ids>           Comma-separated behavior node IDs\n");
    fprintf(out, "  --guid <guid>           Target building-block GUID\n");
    fprintf(out, "  --name <name>           Target behavior name\n");
    fprintf(out, "  --version <n>           Target building-block version (default: 65536)\n");
    fprintf(out, "  --preserve-links        Require control boundary preservation\n");
    fprintf(out, "  --preserve-params       Require parameter boundary preservation\n");
    fprintf(out, "  -o, --output <path>     Future output file path\n");
    fprintf(out, "  --dry-run               Report planned fold without saving (currently required)\n");
}

static void behavior_dump_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior dump [options] [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Dump a compact behavior tree overview.\n\n");
    fprintf(out, "Output:\n");
    fprintf(out, "  Use global -f json or -f json-pretty for machine-readable output.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --all             Dump all script behavior trees\n");
    fprintf(out, "  --flows           Include execution/data flow summaries for one behavior\n");
    fprintf(out, "  --values          Include decoded local/output parameter values\n");
}

static void behavior_find_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior find [options] <file>\n\n");
    fprintf(out, "Search behaviors by name, GUID, or type.\n\n");
    fprintf(out, "Output:\n");
    fprintf(out, "  Use global -f json or -f json-pretty for machine-readable output.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --name <pattern>  Name wildcard pattern\n");
    fprintf(out, "  --guid <guid>     Building block GUID\n");
    fprintf(out, "  --type <type>     Behavior type filter\n");
}

static void behavior_trace_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior trace [options] [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Trace execution path from a behavior IO port.\n\n");
    fprintf(out, "Output:\n");
    fprintf(out, "  Use global -f json or -f json-pretty for machine-readable output.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --from <io_id>   Starting IO port\n");
    fprintf(out, "  --max-depth <n>  Maximum trace depth\n");
}

static void behavior_interface_usage(FILE *out) {
    fprintf(out, "Output:\n");
    fprintf(out, "  Use global -f json or -f json-pretty for machine-readable output.\n\n");
}

static void behavior_add_link_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior add-link --parent <beh-id> --from <io-id> --to <io-id> [options] <file> -o <output>\n\n");
    fprintf(out, "Add a behavior graph link between two IO ports.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -p, --parent <id>      Parent behavior ID (required)\n");
    fprintf(out, "  --from <id>            Source IO port ID (required)\n");
    fprintf(out, "  --to <id>              Target IO port ID (required)\n");
    fprintf(out, "  -d, --delay <n>        Activation delay in frames (default: 1)\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

static void behavior_remove_link_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior remove-link <link-id> --parent <beh-id> <file> -o <output>\n\n");
    fprintf(out, "Remove a behavior graph link and delete the link object.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -p, --parent <id>      Parent behavior ID (required)\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

static void patch_apply_usage(FILE *out) {
    fprintf(out, "Usage: nmo patch apply <patch.json> [--dry-run]\n\n");
    fprintf(out, "Apply a strict behavior rewrite patch.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --dry-run    Preview operations without saving output\n");
}

static void patch_diff_usage(FILE *out) {
    fprintf(out, "Usage: nmo patch diff <patch.json>\n\n");
    fprintf(out, "Print planned behavior rewrite operations without saving output.\n");
}

static void parameter_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo parameter list <file>\n\n");
    fprintf(out, "List parameter objects in the file (CKParameter* family).\n");
}

static void parameter_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo parameter show <id> <file>\n\n");
    fprintf(out, "Show a parameter object with decoded value and type details.\n");
}

static void parameter_dump_usage(FILE *out) {
    fprintf(out, "Usage: nmo parameter dump [--all] [--type <guid>] <id> <file>\n");
    fprintf(out, "       nmo parameter dump --all [--type <guid>] <file>\n\n");
    fprintf(out, "Dump parameter details with decoded values and raw hex.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --all               Dump all parameters in the file\n");
    fprintf(out, "  --type <guid>       Filter by parameter type GUID\n\n");
    fprintf(out, "Examples:\n");
    fprintf(out, "  nmo parameter dump 100 data/Balls.nmo\n");
    fprintf(out, "  nmo parameter dump --all data/Balls.nmo\n");
    fprintf(out, "  nmo parameter dump --all --type {guid} data/Balls.nmo\n");
}

static void parameter_set_usage(FILE *out) {
    fprintf(out, "Usage: nmo parameter set [--id <param-id> | <param-id>] <value> <file> -o <output>\n");
    fprintf(out, "       nmo parameter set --owner <beh-id> --name <name> <value> <file> -o <output>\n");
    fprintf(out, "       nmo parameter set --owner <beh-id> --index <n> <value> <file> -o <output>\n");
    fprintf(out, "       nmo parameter set --hex <param-id> <hex-value> <file> -o <output>\n");
    fprintf(out, "       nmo parameter set --dry-run <param-id> <value> <file>\n\n");
    fprintf(out, "Set a parameter value and save to a new file.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  -b, --owner <id>       Owner behavior/object ID\n");
    fprintf(out, "  -n, --name <name>      Parameter name within owner\n");
    fprintf(out, "  -i, --index <n>        Parameter index within owner\n");
    fprintf(out, "  --hex                  Value is raw hex bytes\n");
    fprintf(out, "  --dry-run              Show old/new without saving\n");
}

static void type_class_tree_usage(FILE *out) {
    fprintf(out, "Usage: nmo type class-tree\n\n");
    fprintf(out, "Show the registered class hierarchy as a tree.\n");
}

/* Convert command usage */
static void convert_copy_usage(FILE *out) {
    fprintf(out, "Usage: nmo convert copy -o <output> [options] <file>\n\n");
    fprintf(out, "Round-trip copy of a file with configurable save options.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required)\n");
    fprintf(out, "  --compress <level>     Compression level (0-9)\n");
    fprintf(out, "  --sequential-ids       Use sequential file IDs\n");
    fprintf(out, "  --no-managers          Exclude manager state\n");
    fprintf(out, "  --strip-resources      Strip included files/resources\n");
    fprintf(out, "  --validate             Validate before writing\n");
    fprintf(out, "  --fast-save            Skip explicit save flush/write-through\n");
}

static void convert_version_usage(FILE *out) {
    fprintf(out, "Usage: nmo convert version [--fast-save] <file>\n");
    fprintf(out, "       nmo convert version [--fast-save] -o <output> <file>\n\n");
    fprintf(out, "Show file version metadata, or copy with version info.\n");
}

static void convert_strip_usage(FILE *out) {
    fprintf(out, "Usage: nmo convert strip -o <output> [options] <file>\n\n");
    fprintf(out, "Remove objects by class or name pattern and save.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --class, -c <name>     Strip objects of this class\n");
    fprintf(out, "  --name <pattern>       Strip objects matching name pattern\n");
    fprintf(out, "  --dry-run              Preview matching objects without modifying\n");
    fprintf(out, "  --fast-save            Skip explicit save flush/write-through\n");
}

static void convert_merge_usage(FILE *out) {
    fprintf(out, "Usage: nmo convert merge [--fast-save] -o <output> <source> <target>\n\n");
    fprintf(out, "Merge objects from source file into target file.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required)\n");
    fprintf(out, "  --fast-save            Skip explicit save flush/write-through\n");
}

static void convert_export_usage(FILE *out) {
    fprintf(out, "Usage: nmo convert export [options] <file> -o <output>\n\n");
    fprintf(out, "Export selected objects to a new NMO file.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --class, -c <name>     Filter by class (includes derived classes)\n");
    fprintf(out, "  --name, -n <pattern>   Filter by name pattern\n");
    fprintf(out, "  --filter, -f <expr>    Filter by DSL expression\n");
    fprintf(out, "  --all                  Export all objects (no filter required)\n");
    fprintf(out, "  --deps                 Include transitive dependencies\n");
    fprintf(out, "  --dry-run              Preview matching objects without writing\n");
    fprintf(out, "  --compress <0-9>       Compression level\n");
    fprintf(out, "  --fast-save            Skip explicit save flush/write-through\n");
}

/* Diff command usage */
static void diff_summary_usage(FILE *out) {
    fprintf(out, "Usage: nmo diff summary <file1> <file2>\n\n");
    fprintf(out, "Show a high-level diff summary between two files.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --ignore-order    Ignore object order differences\n");
}

static void diff_objects_usage(FILE *out) {
    fprintf(out, "Usage: nmo diff objects [options] <file1> <file2>\n\n");
    fprintf(out, "Compare objects using topology-aware matching between two files.\n");
    fprintf(out, "Output uses git-style unified diff format.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --max-objects <N>      Max changed objects to show (default: unlimited)\n");
    fprintf(out, "  --max-fields <N>       Max changed fields per object (default: unlimited)\n");
    fprintf(out, "  --min-similarity <f>   Min match similarity in [0,1] (default: 0.0)\n");
    fprintf(out, "  --rename-similarity <f> Min rename similarity in [0,1] (default: 0.85)\n");
}

static void diff_chunks_usage(FILE *out) {
    fprintf(out, "Usage: nmo diff chunks <file1> <file2>\n\n");
    fprintf(out, "Compare chunks between two files.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --object <id>    Compare a specific object's chunks\n");
}

static void diff_full_usage(FILE *out) {
    fprintf(out, "Usage: nmo diff full <file1> <file2>\n\n");
    fprintf(out, "Comprehensive comparison of two files.\n");
}

/* Query command usage */
static void query_eval_usage(FILE *out) {
    fprintf(out, "Usage: nmo query eval \"<expression>\" <file>\n\n");
    fprintf(out, "Evaluate a single DSL expression against a file.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --expr <expression>    Expression (alternative to positional)\n");
}

static void query_script_usage(FILE *out) {
    fprintf(out, "Usage: nmo query script <script.nmodsl> <file> [-o <output>]\n\n");
    fprintf(out, "Execute a DSL script file. Use -o to save mutated session.\n");
}

static void query_schema_usage(FILE *out) {
    fprintf(out, "Usage: nmo query schema <schema.nmodsl>\n\n");
    fprintf(out, "Apply DSL schema declarations to the type registry.\n");
}

static void query_module_usage(FILE *out) {
    fprintf(out, "Usage: nmo query module <module.nmodsl> <file> [-o <output>]\n\n");
    fprintf(out, "Run a complete DSL module (schema + script).\n");
}

/* Extension command usage */
static void extension_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo extension list\n\n");
    fprintf(out, "List all registered extensions.\n");
}

static void extension_load_usage(FILE *out) {
    fprintf(out, "Usage: nmo extension load <path>\n\n");
    fprintf(out, "Load an extension plugin from a shared library.\n");
}

static void extension_info_usage(FILE *out) {
    fprintf(out, "Usage: nmo extension info <file>\n\n");
    fprintf(out, "Show extension/plugin information for a file.\n");
}

static void extension_check_usage(FILE *out) {
    fprintf(out, "Usage: nmo extension check <file>\n\n");
    fprintf(out, "Check plugin dependencies for a file.\n");
}

/* Texture command usage */
static void texture_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo texture list [options] <file>\n\n");
    fprintf(out, "List all CKTexture objects with dimensions, format, and slot count.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --sort, -s <field>   Sort by: id, name, size\n");
    fprintf(out, "  --top, -t <n>        Show only top N textures\n");
    fprintf(out, "  --reverse, -r        Reverse sort order\n");
}

static void texture_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo texture show [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show detailed texture metadata and per-slot information.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --id <id>            Texture object ID (alternative to positional)\n");
    fprintf(out, "  --name, -n <name>    Exact texture object name\n");
}

static void texture_extract_usage(FILE *out) {
    fprintf(out, "Usage: nmo texture extract --out-dir <dir> [options] <file>\n\n");
    fprintf(out, "Extract textures as image files (PNG, BMP, TGA, JPG).\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --out-dir, -d <dir>  Output directory (required)\n");
    fprintf(out, "  --id <n>             Extract single texture by ID\n");
    fprintf(out, "  --name, -n <pat>     Filter by name wildcard\n");
    fprintf(out, "  --format, -f <fmt>   Output format: png, bmp, tga, jpg (default: png)\n");
    fprintf(out, "  --quality, -q <n>    JPEG quality 1-100 (default: 90)\n");
    fprintf(out, "  --overwrite          Overwrite existing files\n");
}

static void texture_replace_usage(FILE *out) {
    fprintf(out, "Usage: nmo texture replace [--id <id> | --name <name> | <id>] --file <image> <nmo-file> -o <output> [--dry-run]\n\n");
    fprintf(out, "Replace texture bitmap data with image from disk.\n\n");
    fprintf(out, "Loads the image, decodes to RGBA, re-encodes as PNG into the\n");
    fprintf(out, "texture's reader slot, and updates dimensions.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --id <id>            Texture object ID (alternative to positional)\n");
    fprintf(out, "  --name, -n <name>    Exact texture object name\n");
    fprintf(out, "  -f, --file <path>    Image file to load (required)\n");
    fprintf(out, "  -o, --output <path>  Output file (required unless --dry-run)\n");
    fprintf(out, "  --dry-run            Preview without saving\n");
}

/* Data array command usage */
static void data_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo data list <file>\n\n");
    fprintf(out, "List all CKDataArray objects with column and row counts.\n");
}

static void data_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo data show [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show data array metadata and column schema.\n");
}

static void data_dump_usage(FILE *out) {
    fprintf(out, "Usage: nmo data dump [options] [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Dump data array contents as a table.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --row, -r <n>   Dump a single row by index (key-value format)\n");
}

static void data_set_cell_usage(FILE *out) {
    fprintf(out, "Usage: nmo data set-cell [--id <id> | --name <name> | <id>] --row <r> --col <c> --value <val> <file> -o <output>\n\n");
    fprintf(out, "Modify a single cell in a CKDataArray.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -r, --row <n>        Row index (0-based, required)\n");
    fprintf(out, "  -c, --col <n>        Column index (0-based, required)\n");
    fprintf(out, "  -v, --value <val>    New cell value (required)\n");
    fprintf(out, "  -o, --output <path>  Output file (required unless --dry-run)\n");
    fprintf(out, "  --dry-run            Preview without saving\n\n");
    fprintf(out, "Value format depends on column type:\n");
    fprintf(out, "  int:       integer (decimal or 0x hex)\n");
    fprintf(out, "  float:     floating point number\n");
    fprintf(out, "  string:    text string\n");
    fprintf(out, "  object:    object ID (e.g. 42 or #42)\n");
    fprintf(out, "  parameter: parameter ID (e.g. 42 or #42)\n");
}

/* Scene command usage */
static void scene_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo scene list <file>\n\n");
    fprintf(out, "List all CKScene and CKLevel objects.\n");
}

static void scene_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo scene show [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show scene or level details.\n\n");
    fprintf(out, "For CKScene: background, ambient, fog, camera, environment.\n");
    fprintf(out, "For CKLevel: scene list, current scene, level scene.\n");
}

static void scene_set_usage(FILE *out) {
    fprintf(out, "Usage: nmo scene set [--id <id> | --name <name> | <id>] [options] <file> -o <output>\n\n");
    fprintf(out, "Set scene properties.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --id <id>             Scene object ID (alternative to positional)\n");
    fprintf(out, "  --name, -n <name>     Exact scene object name\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --bg-color <color>     Background color (ARGB hex, e.g. 0xFF000000)\n");
    fprintf(out, "  --ambient <color>      Ambient light color (ARGB hex)\n");
    fprintf(out, "  --fog-color <color>    Fog color (ARGB hex)\n");
    fprintf(out, "  --camera <id>          Starting camera object ID\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

/* Entity command usage */
static void entity_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo entity list [options] <file>\n\n");
    fprintf(out, "List all 3D entities (CK3dEntity and derived classes).\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --class, -c <name>   Filter by class (e.g. CKCamera, CKLight)\n");
}

static void entity_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo entity show [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show 3D entity details including transform, mesh, and animations.\n");
    fprintf(out, "For CKCamera: also shows projection, FOV, near/far planes.\n");
    fprintf(out, "For CKLight: also shows light type, color, range, attenuation.\n");
}

static void entity_set_position_usage(FILE *out) {
    fprintf(out, "Usage: nmo entity set-position [--id <id> | --name <name> | <id>] <x> <y> <z> <file> -o <output>\n\n");
    fprintf(out, "Set the world position (translation) of a 3D entity.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

static void entity_set_parent_usage(FILE *out) {
    fprintf(out, "Usage: nmo entity set-parent [--id <id> | --name <name> | <id>] <parent-id> <file> -o <output>\n\n");
    fprintf(out, "Set the parent of a 3D entity. Use 0 to unparent.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

static void entity_set_camera_usage(FILE *out) {
    fprintf(out, "Usage: nmo entity set-camera [--id <id> | --name <name> | <id>] [options] <file> -o <output>\n\n");
    fprintf(out, "Set camera properties on a CKCamera or CKTargetCamera.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --fov <float>          Field of view (radians)\n");
    fprintf(out, "  --near <float>         Near clipping plane\n");
    fprintf(out, "  --far <float>          Far clipping plane\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

static void entity_set_light_usage(FILE *out) {
    fprintf(out, "Usage: nmo entity set-light [--id <id> | --name <name> | <id>] [options] <file> -o <output>\n\n");
    fprintf(out, "Set light properties on a CKLight or CKTargetLight.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --diffuse <color>      Diffuse color\n");
    fprintf(out, "  --range <float>        Light range\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

/* Material command usage */
static void material_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo material list <file>\n\n");
    fprintf(out, "List all CKMaterial objects with diffuse color and texture count.\n");
}

static void material_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo material show [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show material details: colors, specular power, textures, blend modes.\n");
}

static void material_set_usage(FILE *out) {
    fprintf(out, "Usage: nmo material set [--id <id> | --name <name> | <id>] [options] <file> -o <output>\n\n");
    fprintf(out, "Set material properties.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --id <id>             Material object ID (alternative to positional)\n");
    fprintf(out, "  --name, -n <name>     Exact material object name\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --diffuse <color>      Diffuse color (ARGB hex, e.g. 0xFFFF0000)\n");
    fprintf(out, "  --ambient <color>      Ambient color (ARGB hex)\n");
    fprintf(out, "  --specular <color>     Specular color (ARGB hex)\n");
    fprintf(out, "  --emissive <color>     Emissive color (ARGB hex)\n");
    fprintf(out, "  --power <float>        Specular power\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

/* Mesh command usage */
static void mesh_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo mesh list <file>\n\n");
    fprintf(out, "List all CKMesh objects with vertex/face/material counts.\n");
}

static void mesh_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo mesh show [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show mesh details: geometry, bounds, material groups.\n");
}

static void mesh_export_usage(FILE *out) {
    fprintf(out, "Usage: nmo mesh export --out-dir <dir> [--all | --id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Export mesh as Wavefront OBJ + MTL files.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --out-dir, -d <dir>  Output directory (required)\n");
    fprintf(out, "  --id <n>             Export single mesh by ID\n");
    fprintf(out, "  --all, -a            Export all meshes\n");
}

static void mesh_import_usage(FILE *out) {
    fprintf(out, "Usage: nmo mesh import <obj-file> <nmo-file> -o <output> [--replace <id> | --replace-name <name>] [--dry-run]\n\n");
    fprintf(out, "Import a Wavefront OBJ file into an NMO mesh.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --replace <id>         Replace existing mesh by ID\n");
    fprintf(out, "  --replace-name <name>  Replace existing mesh by exact name\n");
    fprintf(out, "  --name, -n <name>      Mesh name\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

/* Animation command usage */
static void animation_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo animation list <file>\n\n");
    fprintf(out, "List all animation objects (CKAnimation, CKKeyedAnimation, CKObjectAnimation).\n");
}

static void animation_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo animation show [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show animation details based on class type.\n");
}

static void animation_keys_usage(FILE *out) {
    fprintf(out, "Usage: nmo animation keys [--id <id> | --name <name> | <id>] <file>\n\n");
    fprintf(out, "Show decoded key data for a CKObjectAnimation.\n");
    fprintf(out, "Decodes position, rotation, and other controller keys.\n");
}

static void animation_export_usage(FILE *out) {
    fprintf(out, "Usage: nmo animation export [--all | --id <id> | --name <name> | <id>] --out-dir <dir> <file>\n\n");
    fprintf(out, "Export CKObjectAnimation data as JSON.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --out-dir, -d <dir>  Output directory (required)\n");
    fprintf(out, "  --id, -i <n>         Export single animation by ID\n");
    fprintf(out, "  --all                Export all CKObjectAnimation objects\n");
}

static void animation_import_usage(FILE *out) {
    fprintf(out, "Usage: nmo animation import <json-file> <nmo-file> -o <output> [--replace <id> | --replace-name <name>] [--dry-run]\n\n");
    fprintf(out, "Import animation data from JSON into an NMO file.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -o, --output <path>    Output file (required unless --dry-run)\n");
    fprintf(out, "  --replace <id>         Replace existing animation by ID\n");
    fprintf(out, "  --replace-name <name>  Replace existing animation by exact name\n");
    fprintf(out, "  --dry-run              Preview without saving\n");
}

static void completion_usage(FILE *out) {
    fprintf(out, "Usage: nmo completion <bash|fish|zsh|powershell|ps1>\n\n");
    fprintf(out, "Print the shell completion script for the selected shell to stdout.\n");
}

/* ============================================================================
 * Action definitions for each group
 * ============================================================================ */

#define ACTION(name, alias, brief, handler, usage, policy) \
    {name, alias, brief, handler, usage, NULL, 0, NULL, policy}
#define ACTION_SUB(name, alias, brief, usage, subs, sub_count, default_sub, policy) \
    {name, alias, brief, NULL, usage, subs, sub_count, default_sub, policy}

/* file group actions */
static const nmo_cli_action_t file_actions[] = {
    ACTION("info", "i", "Show file summary", nmo_cmd_file_info, file_info_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("header", "hdr", "Show file header fields", nmo_cmd_file_header, file_header_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("stats", "st", "Show file statistics", nmo_cmd_file_stats, file_stats_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("classes", "cls", "Show class ID distribution", nmo_cmd_file_classes, file_classes_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("plugins", "pl", "Show plugin dependencies", nmo_cmd_file_plugins, file_plugins_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("space", "sp", "Analyze space usage", nmo_cmd_file_space, file_space_usage, NMO_REPL_ACTION_READ_SESSION),
};

static const nmo_cli_action_t chunk_actions[] = {
    ACTION("list", "ls", "List all chunks", nmo_cmd_chunk_list, chunk_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("tree", "t", "Show chunk hierarchy", nmo_cmd_chunk_tree, chunk_tree_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show chunk details", nmo_cmd_chunk_show, chunk_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("find", "f", "Find chunks by class/name", nmo_cmd_chunk_find, chunk_find_usage, NMO_REPL_ACTION_READ_SESSION),
};

static const nmo_cli_action_t object_actions[] = {
    ACTION("list", "ls", "List objects", nmo_cmd_object_list, object_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("tree", "t", "Show object hierarchy", nmo_cmd_object_tree, object_tree_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show object details", nmo_cmd_object_show, object_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("find", "f", "Find objects by query", nmo_cmd_object_find, object_find_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("refs", "r", "Show object references", nmo_cmd_object_refs, object_refs_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("rename", "ren", "Rename an object", nmo_cmd_object_rename, object_rename_usage, NMO_REPL_ACTION_MUTATE_SESSION_SUPPORTED),
    ACTION("export", "x", "Export importable object snapshot", nmo_cmd_object_export, object_export_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("impact", "imp", "Show deletion impact", nmo_cmd_object_impact, object_impact_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("orphans", "orp", "Find unreachable objects", nmo_cmd_object_orphans, object_orphans_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("cycles", "cyc", "Detect circular references", nmo_cmd_object_cycles, object_cycles_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("delete", "del", "Delete objects", nmo_cmd_object_delete, object_delete_usage, NMO_REPL_ACTION_MUTATE_SESSION_SUPPORTED),
    ACTION("create", NULL, "Create new object", nmo_cmd_object_create, object_create_usage, NMO_REPL_ACTION_MUTATE_SESSION_SUPPORTED),
    ACTION("copy", "cp", "Copy objects", nmo_cmd_object_copy, object_copy_usage, NMO_REPL_ACTION_MUTATE_SESSION_SUPPORTED),
    ACTION("import", NULL, "Import object export snapshot", nmo_cmd_object_import, object_import_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("graph", "gr", "Export reference graph", nmo_cmd_object_graph, object_graph_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("set-field", "sf", "Set typed field value", nmo_cmd_object_set_field, object_set_field_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("list-fields", "lf", "List object fields and values", nmo_cmd_object_list_fields, object_list_fields_usage, NMO_REPL_ACTION_READ_SESSION),
};

static const nmo_cli_action_t behavior_actions[] = {
    ACTION("list", "ls", "List behaviors", nmo_cmd_behavior_list, behavior_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("stats", "st", "Show behavior statistics", nmo_cmd_behavior_stats, behavior_stats_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show behavior signature", nmo_cmd_behavior_show, behavior_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("graph", "g", "Export behavior graph", nmo_cmd_behavior_graph, behavior_graph_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("graph-boundary", NULL, "Export behavior graph boundary", nmo_cmd_behavior_graph_boundary, behavior_graph_boundary_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("replace-bb", NULL, "Replace a leaf building block", nmo_cmd_behavior_replace_bb, behavior_replace_bb_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("fold", NULL, "Analyze subgraph fold rewrite", nmo_cmd_behavior_fold, behavior_fold_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("fold-candidates", NULL, "Report fold candidate boundaries", nmo_cmd_behavior_fold_candidates, behavior_fold_candidates_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("dump", "d", "Dump compact behavior tree overview", nmo_cmd_behavior_dump, behavior_dump_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("find", "f", "Search behaviors by name/GUID/type", nmo_cmd_behavior_find, behavior_find_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("trace", "tr", "Trace execution path from IO", nmo_cmd_behavior_trace, behavior_trace_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("add-link", NULL, "Add behavior graph link", nmo_cmd_behavior_add_link, behavior_add_link_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("remove-link", NULL, "Remove behavior graph link", nmo_cmd_behavior_remove_link, behavior_remove_link_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION_SUB("interface", "iface", "Interface layout commands", behavior_interface_usage,
        nmo_behavior_interface_sub_actions, NMO_BEHAVIOR_INTERFACE_SUB_ACTION_COUNT, "show", NMO_REPL_ACTION_READ_SESSION),
};

static const nmo_cli_action_t patch_actions[] = {
    ACTION("apply", NULL, "Apply rewrite patch", nmo_cmd_patch_apply, patch_apply_usage, NMO_REPL_ACTION_FORBIDDEN),
    ACTION("diff", NULL, "Preview rewrite patch", nmo_cmd_patch_diff, patch_diff_usage, NMO_REPL_ACTION_FORBIDDEN),
};

static const nmo_cli_action_t parameter_actions[] = {
    ACTION("list", "ls", "List parameters", nmo_cmd_parameter_list, parameter_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show parameter object", nmo_cmd_parameter_show, parameter_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("dump", "d", "Dump parameter with decoded value", nmo_cmd_parameter_dump, parameter_dump_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("set", NULL, "Set parameter value", nmo_cmd_parameter_set, parameter_set_usage, NMO_REPL_ACTION_MUTATE_SESSION_SUPPORTED),
};

static const nmo_cli_action_t script_actions[] = {
    ACTION("graph", "g", "Export script edit graph", nmo_cmd_script_graph, script_graph_usage, NMO_REPL_ACTION_READ_SESSION),
};

static const nmo_cli_action_t resource_actions[] = {
    ACTION("list", "ls", "List resources", nmo_cmd_resource_list, resource_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show resource details", nmo_cmd_resource_show, resource_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("extract", "x", "Extract embedded resources", nmo_cmd_resource_extract, resource_extract_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("import", "imp", "Import resource from disk", nmo_cmd_resource_import, resource_import_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("replace", "rep", "Replace resource payload", nmo_cmd_resource_replace, resource_replace_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("remove", "rm", "Remove included file", nmo_cmd_resource_remove, resource_remove_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("info", NULL, "Detect resource format", nmo_cmd_resource_info, resource_info_usage, NMO_REPL_ACTION_READ_SESSION),
};

static const nmo_cli_action_t type_actions[] = {
    ACTION("list", "ls", "List registered types", nmo_cmd_type_list, type_list_usage, NMO_REPL_ACTION_READ_NO_SESSION),
    ACTION("show", "s", "Show type details", nmo_cmd_type_show, type_show_usage, NMO_REPL_ACTION_READ_NO_SESSION),
    ACTION("class-tree", "ct", "Show class hierarchy tree", nmo_cmd_type_class_tree, type_class_tree_usage, NMO_REPL_ACTION_READ_NO_SESSION),
};

static const nmo_cli_action_t validate_actions[] = {
    ACTION("all", "a", "Run all validation checks", nmo_cmd_validate_all, validate_all_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("structure", "st", "Validate file structure", nmo_cmd_validate_structure, validate_structure_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("references", "ref", "Validate object references", nmo_cmd_validate_references, validate_references_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("resources", "res", "Validate embedded resources", nmo_cmd_validate_resources, validate_resources_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("orphans", "orp", "Find unreferenced objects", nmo_cmd_validate_orphans, validate_orphans_usage, NMO_REPL_ACTION_READ_SESSION),
};

static const nmo_cli_action_t convert_actions[] = {
    ACTION("copy", "cp", "Round-trip copy with save options", nmo_cmd_convert_copy, convert_copy_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("version", "v", "Show/modify file version metadata", nmo_cmd_convert_version, convert_version_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("strip", "st", "Remove objects by class/name pattern", nmo_cmd_convert_strip, convert_strip_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("merge", "m", "Merge objects from source into target", nmo_cmd_convert_merge, convert_merge_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("export", "x", "Export selected objects to new file", nmo_cmd_convert_export, convert_export_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
};

static const nmo_cli_action_t diff_actions[] = {
    ACTION("summary", "s", "Show diff summary", nmo_cmd_diff_summary, diff_summary_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("objects", "obj", "Diff objects between files", nmo_cmd_diff_objects, diff_objects_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("chunks", "ch", "Diff chunks between files", nmo_cmd_diff_chunks, diff_chunks_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("full", "f", "Full comparison", nmo_cmd_diff_full, diff_full_usage, NMO_REPL_ACTION_READ_SESSION),
};

static const nmo_cli_action_t query_actions[] = {
    ACTION("eval", "e", "Evaluate DSL expression", nmo_cmd_query_eval, query_eval_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("script", "s", "Execute DSL script", nmo_cmd_query_script, query_script_usage, NMO_REPL_ACTION_FORBIDDEN),
    ACTION("schema", "sc", "Apply DSL schema", nmo_cmd_query_schema, query_schema_usage, NMO_REPL_ACTION_FORBIDDEN),
    ACTION("module", "m", "Run DSL module", nmo_cmd_query_module, query_module_usage, NMO_REPL_ACTION_FORBIDDEN),
};

static const nmo_cli_action_t extension_actions[] = {
    ACTION("list", "ls", "List registered extensions", nmo_cmd_extension_list, extension_list_usage, NMO_REPL_ACTION_READ_NO_SESSION),
    ACTION("load", "ld", "Load extension DLL", nmo_cmd_extension_load, extension_load_usage, NMO_REPL_ACTION_FORBIDDEN),
    ACTION("info", "i", "Query extension metadata", nmo_cmd_extension_info, extension_info_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("check", "ch", "Check plugin dependencies", nmo_cmd_extension_check, extension_check_usage, NMO_REPL_ACTION_READ_SESSION),
};

static const nmo_cli_action_t texture_actions[] = {
    ACTION("list", "ls", "List textures", nmo_cmd_texture_list, texture_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show texture details", nmo_cmd_texture_show, texture_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("extract", "x", "Extract textures as images", nmo_cmd_texture_extract, texture_extract_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("replace", "rep", "Replace texture bitmap data", nmo_cmd_texture_replace, texture_replace_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
};

static const nmo_cli_action_t data_actions[] = {
    ACTION("list", "ls", "List data arrays", nmo_cmd_data_list, data_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show data array schema", nmo_cmd_data_show, data_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("dump", "d", "Dump data array contents", nmo_cmd_data_dump, data_dump_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("set-cell", "sc", "Modify a single cell", nmo_cmd_data_set_cell, data_set_cell_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
};

static const nmo_cli_action_t scene_actions[] = {
    ACTION("list", "ls", "List scenes and levels", nmo_cmd_scene_list, scene_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show scene/level details", nmo_cmd_scene_show, scene_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("set", NULL, "Set scene properties", nmo_cmd_scene_set, scene_set_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
};

static const nmo_cli_action_t entity_actions[] = {
    ACTION("list", "ls", "List 3D entities", nmo_cmd_entity_list, entity_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show entity details", nmo_cmd_entity_show, entity_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("set-position", "sp", "Set entity position", nmo_cmd_entity_set_position, entity_set_position_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("set-parent", NULL, "Set entity parent", nmo_cmd_entity_set_parent, entity_set_parent_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("set-camera", "sc", "Set camera properties", nmo_cmd_entity_set_camera, entity_set_camera_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
    ACTION("set-light", "sl", "Set light properties", nmo_cmd_entity_set_light, entity_set_light_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
};

static const nmo_cli_action_t material_actions[] = {
    ACTION("list", "ls", "List materials", nmo_cmd_material_list, material_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show material details", nmo_cmd_material_show, material_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("set", NULL, "Set material properties", nmo_cmd_material_set, material_set_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
};

static const nmo_cli_action_t mesh_actions[] = {
    ACTION("list", "ls", "List meshes", nmo_cmd_mesh_list, mesh_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show mesh details", nmo_cmd_mesh_show, mesh_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("export", "x", "Export mesh as OBJ", nmo_cmd_mesh_export, mesh_export_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("import", "imp", "Import OBJ into mesh", nmo_cmd_mesh_import, mesh_import_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
};

static const nmo_cli_action_t animation_actions[] = {
    ACTION("list", "ls", "List animation objects", nmo_cmd_animation_list, animation_list_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("show", "s", "Show animation details", nmo_cmd_animation_show, animation_show_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("keys", "k", "Show decoded key data", nmo_cmd_animation_keys, animation_keys_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("export", "x", "Export animation as JSON", nmo_cmd_animation_export, animation_export_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("import", "imp", "Import animation from JSON", nmo_cmd_animation_import, animation_import_usage, NMO_REPL_ACTION_MUTATE_FILE_ONLY),
};

static const nmo_cli_action_t completion_actions[] = {
    ACTION("bash", NULL, "Print Bash completion", nmo_cmd_completion_print, completion_usage, NMO_REPL_ACTION_READ_NO_SESSION),
    ACTION("fish", NULL, "Print Fish completion", nmo_cmd_completion_print, completion_usage, NMO_REPL_ACTION_READ_NO_SESSION),
    ACTION("zsh", NULL, "Print Zsh completion", nmo_cmd_completion_print, completion_usage, NMO_REPL_ACTION_READ_NO_SESSION),
    ACTION("powershell", "ps1", "Print PowerShell completion", nmo_cmd_completion_print, completion_usage, NMO_REPL_ACTION_READ_NO_SESSION),
};

static const nmo_cli_action_t debug_actions[] = {
    ACTION("load-phases", "lp", "Show load pipeline phases", nmo_cmd_debug_load_phases, debug_load_phases_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("chunks", "ch", "Show chunk parse details", nmo_cmd_debug_chunks, debug_chunks_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("objects", "obj", "Show object load details", nmo_cmd_debug_objects, debug_objects_usage, NMO_REPL_ACTION_READ_SESSION),
    ACTION("export", "x", "Export JSON snapshot for debugging", nmo_cmd_debug_export, debug_export_usage, NMO_REPL_ACTION_READ_SESSION),
};

static const nmo_cli_action_t repl_actions[] = {
    ACTION("start", NULL, "Start interactive REPL", NULL, repl_start_usage, NMO_REPL_ACTION_FORBIDDEN),
};

#undef ACTION
#undef ACTION_SUB

/* ============================================================================
 * Group definitions
 * ============================================================================ */

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const nmo_cli_group_t groups[] = {
    {"file", "f", "File information and statistics", file_actions, ARRAY_SIZE(file_actions), nmo_cmd_file_in_session},
    {"chunk", "ch", "Chunk inspection", chunk_actions, ARRAY_SIZE(chunk_actions), nmo_cmd_chunk_in_session},
    {"object", "obj", "Object inspection", object_actions, ARRAY_SIZE(object_actions), nmo_cmd_object_in_session},
    {"behavior", "beh", "Behavior inspection", behavior_actions, ARRAY_SIZE(behavior_actions), nmo_cmd_behavior_in_session},
    {"patch", NULL, "Patch apply and diff", patch_actions, ARRAY_SIZE(patch_actions), NULL},
    {"parameter", "param", "Parameter inspection", parameter_actions, ARRAY_SIZE(parameter_actions), nmo_cmd_parameter_in_session},
    {"script", NULL, "Script editing and graph queries", script_actions, ARRAY_SIZE(script_actions), nmo_cmd_script_in_session},
    {"resource", "res", "Resource management", resource_actions, ARRAY_SIZE(resource_actions), nmo_cmd_resource_in_session},
    {"texture", "tex", "Texture management", texture_actions, ARRAY_SIZE(texture_actions), nmo_cmd_texture_in_session},
    {"data", "da", "Data array inspection", data_actions, ARRAY_SIZE(data_actions), nmo_cmd_data_in_session},
    {"scene", "sc", "Scene/level inspection", scene_actions, ARRAY_SIZE(scene_actions), nmo_cmd_scene_in_session},
    {"entity", "ent", "3D entity inspection", entity_actions, ARRAY_SIZE(entity_actions), nmo_cmd_entity_in_session},
    {"material", "mat", "Material inspection", material_actions, ARRAY_SIZE(material_actions), nmo_cmd_material_in_session},
    {"mesh", "m", "Mesh inspection and export", mesh_actions, ARRAY_SIZE(mesh_actions), nmo_cmd_mesh_in_session},
    {"animation", "anim", "Animation inspection and export", animation_actions, ARRAY_SIZE(animation_actions), nmo_cmd_animation_in_session},
    {"type", "t", "Type system information", type_actions, ARRAY_SIZE(type_actions), NULL},
    {"validate", "val", "File validation", validate_actions, ARRAY_SIZE(validate_actions), nmo_cmd_validate_in_session},
    {"convert", "conv", "Format conversion", convert_actions, ARRAY_SIZE(convert_actions), NULL},
    {"diff", "d", "File comparison", diff_actions, ARRAY_SIZE(diff_actions), nmo_cmd_diff_in_session},
    {"query", "q", "DSL query engine", query_actions, ARRAY_SIZE(query_actions), nmo_cmd_query_in_session},
    {"extension", "ext", "Extension management", extension_actions, ARRAY_SIZE(extension_actions), nmo_cmd_extension_in_session},
    {"completion", "comp", "Shell completion scripts", completion_actions, ARRAY_SIZE(completion_actions), NULL},
    {"debug", "dbg", "Debugging tools", debug_actions, ARRAY_SIZE(debug_actions), nmo_cmd_debug_in_session},
    {"repl", NULL, "Interactive debugger", repl_actions, ARRAY_SIZE(repl_actions), NULL},
};

static const size_t group_count = ARRAY_SIZE(groups);

/* ============================================================================
 * Lookup functions
 * ============================================================================ */

const nmo_cli_group_t *nmo_command_registry_find_group(const char *name,
                                                       bool allow_alias) {
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < group_count; ++i) {
        const nmo_cli_group_t *g = &groups[i];
        if (nmo_tool_streq_ci(name, g->name)) {
            return g;
        }
        if (allow_alias && g->alias && nmo_tool_streq_ci(name, g->alias)) {
            return g;
        }
    }
    return NULL;
}

const nmo_cli_action_t *nmo_command_registry_find_action(
    const nmo_cli_group_t *group,
    const char *name,
    bool allow_alias) {
    if (!group || !name) {
        return NULL;
    }
    for (size_t i = 0; i < group->action_count; ++i) {
        const nmo_cli_action_t *a = &group->actions[i];
        if (nmo_tool_streq_ci(name, a->name)) {
            return a;
        }
        if (allow_alias && a->alias && nmo_tool_streq_ci(name, a->alias)) {
            return a;
        }
    }
    return NULL;
}

const nmo_cli_group_t *nmo_command_registry_get_groups(size_t *count) {
    if (count) {
        *count = group_count;
    }
    return groups;
}

int nmo_command_registry_dispatch_read_in_session(
    const nmo_cli_group_t *group,
    const nmo_cli_action_t *action,
    nmo_cmd_ctx_t *ctx,
    int argc,
    char **argv)
{
    if (!group || !ctx || argc < 1 || !argv || !argv[0]) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!action) {
        action = nmo_command_registry_find_action(group, argv[0], true);
    }
    if (!action || action->repl_policy != NMO_REPL_ACTION_READ_SESSION) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!group->repl_session_handler) {
        fprintf(stderr, "Internal error: no in-session dispatcher for %s\n",
                group->name ? group->name : "(unknown)");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return group->repl_session_handler(ctx, argc, argv);
}
