# Script Edit Report Contracts

This fixture freezes the v1 report contracts and option mapping for script editing.
Tests may assert the required fields listed here. Fields listed as non-stable must not
be asserted exactly.

## `nmo script graph ... -f json`

Required top-level keys:

- `schema_version`
- `tool`
- `command`
- `timestamp`
- `input_file`
- `data`

Required stable top-level values:

- `tool == "nmo"`
- `command == "script.graph"`

Required `data` keys:

- `root_behavior_id`
- `edit_ready`
- `owner_index_available`
- `node_count`
- `reference_validation`
- `nodes`
- `control_edges`
- `data_edges`

Required `data.reference_validation` keys:

- `status`
- `status_name`
- `broken_count`

Required `data.nodes[]` keys:

- `object_id`
- `kind`
- `class_id`
- `depth`
- `parent_behavior_id`
- `owner_behavior_id`
- `owner_slot_index`
- `owner_slot_kind`

Optional but stable when present in `data.nodes[]`:

- `name`
- `class_name`

Stable enum/string values:

- `data.nodes[].kind` is one of `behavior`, `io`, `parameter`, `operation`, `link`

Required `data.control_edges[]` keys:

- `link_id`
- `source`
- `target`
- `activation_delay`
- `initial_activation_delay`

Required `data.control_edges[].source` and `.target` keys:

- `object_id`
- `owner_behavior_id`
- `owner_index`
- `kind`

Required `data.data_edges[]` keys:

- `source_parameter_id`
- `target_parameter_id`
- `source_owner_id`
- `target_owner_id`
- `type_guid`
- `shared`

Explicitly non-stable:

- `timestamp`
- `input_file`
- `schema_version`
- array ordering for `nodes`, `control_edges`, and `data_edges` unless a test explicitly
  documents the expected traversal order

## `nmo script boundary ... -f json`

This schema is frozen before the command lands so later tasks target one JSON shape.

Required top-level keys:

- `schema_version`
- `tool`
- `command`
- `timestamp`
- `input_file`
- `data`

Required stable top-level values:

- `tool == "nmo"`
- `command == "script.boundary"`

Required `data` keys:

- `parent_id`
- `selected_nodes`
- `internal_nodes`
- `control_in`
- `control_out`
- `parameter_in`
- `parameter_out`
- `broken_links`
- `missing_nodes`
- `edit_ready`

Required `data.control_in[]` and `data.control_out[]` keys:

- `link_id`
- `source_owner_id`
- `source_io_id`
- `target_owner_id`
- `target_io_id`
- `activation_delay`
- `initial_activation_delay`

Required `data.parameter_in[]` and `data.parameter_out[]` keys:

- `source_parameter_id`
- `target_parameter_id`
- `source_owner_id`
- `target_owner_id`
- `type_guid`
- `shared`

Explicitly non-stable:

- `timestamp`
- `input_file`
- `schema_version`

## `nmo script fold ... --dry-run -f json`

This schema is frozen before the command lands so rewrite and Lua layers converge on one
dry-run report contract.

Required top-level keys:

- `schema_version`
- `tool`
- `command`
- `timestamp`
- `input_file`
- `data`

Required stable top-level values:

- `tool == "nmo"`
- `command == "script.fold"`

Required `data` keys:

- `ok`
- `dry_run`
- `parent_id`
- `anchor_id`
- `selected_nodes`
- `guid`
- `name`
- `interface_mode`
- `planned`
- `validation`

Required `data.planned` keys:

- `new_behavior_id`
- `nodes_to_delete`
- `links_to_delete`
- `links_to_retarget`
- `parameter_rewire`
- `maps`

Required `data.validation` keys:

- `edit_ready`
- `reference_status`
- `reference_status_name`
- `broken_reference_count`
- `behavior_index_ok`

Stable enum/string values:

- `interface_mode` is one of `preserve`, `canonicalize`, `remove`

Explicitly non-stable:

- `timestamp`
- `input_file`
- `schema_version`
- predicted object ids allocated during dry-run planning

## `nmo script run ... --dry-run -f json`

This schema is frozen before the Lua bridge lands so the executor and CLI report one
stable transaction contract.

Required top-level keys:

- `schema_version`
- `tool`
- `command`
- `timestamp`
- `input_file`
- `data`

Required stable top-level values:

- `tool == "nmo"`
- `command == "script.run"`

Required `data` keys:

- `ok`
- `dry_run`
- `script_file`
- `op_count`
- `operations`
- `validation`
- `result_handles`

Required `data.operations[]` keys:

- `index`
- `kind`
- `options`

Required `data.validation` keys:

- `references`
- `behavior_index`
- `interface`
- `final_status`
- `final_status_name`

Explicitly non-stable:

- `timestamp`
- `input_file`
- `schema_version`
- executor timing fields if later added
- Lua traceback strings beyond substring/code assertions
- dry-run predicted ids inside `result_handles`

## CLI To Lua Option Mapping

The v1 mapping is fixed:

- `--parent` -> `{ parent = <id> }`
- `--node` -> `{ node = <id> }`
- `--bb-guid` -> `{ guid = "<guid>" }`
- `--name` -> `{ name = "<name>" }`
- `--interface preserve|canonicalize|remove` -> `{ interface = "..." }`

Rules:

- Lua API keys are snake_case.
- CLI long option names map 1:1 to Lua table fields after removing the `--` prefix
  and converting `-` to `_`.
- v1 does not support alternate aliases for the same field.
