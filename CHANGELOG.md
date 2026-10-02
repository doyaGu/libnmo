# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Versions 1.2.0 to 1.4.0 at the end of this file are milestones of an earlier numbering and were
never tagged; the numbering restarted at 0.2.0.

## [Unreleased]

Changes since 0.2.0 (`5005aac3`, 2026-07-16). About 1,000 commits of July to September are summarized
by theme in the sections marked "July to September"; the other sections record the work from
September 30 on in detail.

### Upgrading from 0.2.0
What breaks source compatibility, in short; the sections below give the details.
- The edit stack, the Lua runtime and the project layer are the separate libraries `nmo::edit`,
  `nmo::lua` and `nmo::project`, and seven `behavior/` headers moved to `edit/` (see "Edit, Lua and
  project layers are separate libraries").
- 20 public headers are gone, seven of them moved to `edit/`, and 163 `NMO_API` functions were
  removed (see "Unused APIs"). The chunk parser and writer objects give way to the `nmo_chunk_*`
  functions of `format/nmo_chunk_api.h`, and the `nmo_io_file_*` and `nmo_io_memory_*` functions to
  `nmo_file_io_open()`, `nmo_memory_io_open_read()` and `nmo_memory_io_open_write()`.
- Headers moved to the layer of their code; the old paths stay as forwarders:
  `runtime/nmo_context.h` is now `object/nmo_context.h`, `document/nmo_document.h` is
  `runtime/nmo_document.h`, `behavior/nmo_behavior_registry.h` is `extension/nmo_behavior_registry.h`
  and `document/nmo_document_perf_stats.h` is `format/nmo_perf_stats.h`. The file state types moved
  from `document/nmo_document_load.h` to `format/nmo_file_state.h`, and the workspace edit flags
  to `object/nmo_edit_flags.h`; both are included where they were before.
- `nmo_chunk_file_context_t.repository` is replaced by `ref_tokens`; set it with
  `nmo_object_repository_ref_tokens(repository)`.
- Three signatures changed: `nmo_behavior_normalize_references()` takes the type registry,
  `nmo_interface_graph_io_set_array()` takes the tag array of the ports, and
  `nmo_object_format_path()` returns the full length of the path, as snprintf does.
- State structs of the built-in classes changed, so code that reads them needs a rebuild and a look
  at the members it uses. Renamed or reinterpreted: the skin vertex buffers (`bone_weights` first),
  `nmo_curvepoint_state_t.tangent_mode` (was `use_tcb`, with the opposite meaning), the patch channel
  dwords (`source_blend`, `dest_blend`, `flags`), the legacy texture format fields, the camera's
  `aspect_width` and `aspect_height`, the morph sections of an object animation, and `nmo_chunk_t`,
  which has no `raw_data` any more.
- Changed values: `NMO_CKTEXTURE_USEGLOBAL` and `NMO_CKTEXTURE_INCLUDEORIGINALFILE` are 3 and 4, the
  `data_size` of a CONTROLLERS controller leaves out its key count, `nmo_objanim_controller_key_size()`
  returns 0 for bezier controllers, raw bitmap planes are in blue, green, red, alpha order,
  `nmo_image_reconstruct_pixels()` returns rows top-down, and the hash values of most classes differ.
- Changed behavior: a default save keeps the original chunk of every object that did not change, a
  checksum mismatch on load is a warning (`NMO_LOAD_VERIFY_CRC` makes it an error), and files older
  than version 7 are refused at load.

### Added - Build, install and release packages (July to September)
- Lua 5.5.1 is downloaded and SHA-256 verified at configure time (FetchContent), and isocline, the REPL
  line editor, comes from `deps/isocline/` or a pinned archive. miniz is compiled into libnmo; the
  bundled miniz is 3.1.2, yyjson 0.13.0 and stb 2c980bb5.
- `cmake --install` installs a CMake package (`find_package(libnmo CONFIG)`, `nmo::nmo`), pkg-config
  files, the Lua headers and the licenses of libnmo and its bundled dependencies.
  `tools/scripts/package_release.py` builds a release package.
- CI builds and tests on Linux, macOS (universal), Windows MSVC and Windows MinGW and under ASan and
  UBSan, packages every platform, and publishes the packages as a GitHub release for a `vX.Y.Z` tag.
  Everything builds with `-Wall -Wextra -Wpedantic -Wshadow -Werror`, and with `/W4 /WX` under MSVC.
- A test that needs a Virtools sample that is not there reports itself skipped (exit code 77;
  `TEST_SKIP`, `TEST_REQUIRE_FILE`, `TEST_REQUIRE_FIXTURE`). CTest also runs source audits (no byte
  order marks or mojibake, no upward include outside `tests/layering_allowlist.txt`) and checks that
  the generated CLI completions, object enums and Lua pushers match their generators.
- `docs/api-tiers.md` states what each API tier promises; the CLI and API reference moved from the
  README to `docs/cli-reference.md` and `docs/api.md`.

### Added - Execution attachments and schema helpers
- `nmo_behavior_execution_set_attachment()` / `nmo_behavior_execution_get_attachment()`:
  per-execution data owned by optional components.
- `nmo_chunk_identifier_remaining_dwords()` and `nmo_hash_fnv1a32_update()`, shared by the
  built-in schemas.
- Animation controller helpers in `object/builtin/nmo_animation_schemas.h`:
  `nmo_objanim_controller_format_key_size()` (bytes per key in a given animation format),
  `nmo_objanim_controller_keys_size()` (bytes taken by N consecutive keys),
  `nmo_objanim_controller_is_bezier()` and `nmo_objanim_bezier_key_decode()` with
  `nmo_objanim_bezier_key_t` (one packed bezier key).

### Added - File checksum check
- Loading computes the header checksum the way `CKFile` does for the file version (Adler32 over both
  header parts, the packed Header1 and the packed data for version 8 and later; over the data section
  below that) and compares it with the stored one. `nmo_file_info_t` gained `crc_status`
  (`NMO_CRC_NOT_CHECKED`, `NMO_CRC_OK`, `NMO_CRC_MISMATCH`), `crc_stored` and `crc_computed`.
- A mismatch is logged as a warning and the file still opens, so a file edited by another tool can be
  repaired. CK2 refuses such a file (`CKERR_FILECRCERROR`); `NMO_LOAD_VERIFY_CRC` does the same and
  fails with `NMO_ERR_CHECKSUM_MISMATCH`.
- `nmo validate checksum <file>` (alias `crc`) prints both values, and `--strict` exits with code 3 on a
  mismatch. `nmo validate all` reports a mismatch as an error.
- 512 of the 513 corpus files carry the checksum the engine computes. `Ballance/base.cmo` stores
  0xC6C7A400 where the computation, checked independently, gives 0x196CA3FC; `validate all` on it
  reports that one error. `test_file_checksum` covers a generated file, a flipped header field, a
  flipped stored checksum and the whole corpus.

### Changed - CLI output comes from records (July to September)
- Every command builds its output as a record (`tools/nmo_cli_record.h`) and renders it as text,
  `json` or `json-pretty`, instead of printing the text and building the JSON tree separately, and the
  options of the commands come from shared specs (`tools/nmo_opt.h`). Text is formatted into heap
  strings of the exact size instead of fixed-size buffers.
- Packed ARGB fields print as `0xAARRGGBB` and accept `#` or `0x` hex as well as a float tuple; they
  were read as a 16-byte float color. Snapshots leave out `raw_hex` for values that hold process
  addresses or strings, and object summaries describe pointer fields instead of printing addresses.

### Changed - Objects are handled by their effective type (July to September)
- An object whose type GUID was set (`nmo_object_set_type_guid()`) is loaded, saved, validated and
  edited through that type instead of the one registered for its class id, and so are the behavior
  graph, script, semantic validator and probe analyzer views of it. `nmo_type_query_find_for_object()`
  and `nmo_type_query_object_is_derived_from_class()` do the lookup.

### Changed - Editing (July to September)
- Workspace edit transactions reclaim what a failed edit allocated and keep their journal in a unit of
  its own; object creation, entity writes and camera and light settings go through workspace edits;
  behavior edit snapshots are deep copies; the fold and replace-bb rewrites run inside script edit
  transactions and rewire their boundary through script edit primitives.
- The edit op kinds have one metadata table (`nmo_edit_op_kind_name()`, `nmo_edit_op_kind_parse()`),
  and the JSON of the op kinds with flat scalar payloads is encoded and decoded from field tables.
- `script_edit.c`, `edit_plan.c` and `behavior_rewrite.c` are split by operation family.

### Changed - State hooks and older data versions (July to September)
- The copy, equals and hash hooks compare by content (reflected arrays, nested records) and copy
  transactionally: a failed copy leaves the destination as it was. Arrays are validated before a
  state is serialized or copied, and fixed caps on collection sizes (for example 100,000 scene
  objects) gave way to what the format can encode.
- Chunks of older data versions are written back in their own layout (wave sound, behavior, 2D
  entity, character, body part joints, curve, curve point, legacy scale-axis controllers), while new
  chunks and states use the current version.
- A section longer than the fields of its class was rejected; the CK2_3D classes accept the extra
  dwords again, as the engine does (see "Loading accepts what the engine accepts").

### Changed - References are kept and checked (July to September)
- Every built-in class keeps its object references as the `nmo_ref_t` records of 0.2.0, so an
  unresolved, null or mismatched reference survives load and save: levels, synchros, scenes,
  behavior links, materials, parameters, legacy attributes, characters, meshes, patch meshes,
  animations, curves, behaviors, data array cells, skin bones and place portals.
- References are checked against the classes their field allows (parameter inputs, outputs and
  operations, synchro waiters, scene descriptors, place children, character animations, BeObject
  attribute parameters, behavior owners, data array cells). Normalizing invalid references
  (`nmo_runtime_normalize_object_invalid_refs()`) reports each object and leaves the chunks of the
  other objects and the compression of the file as they were.

### Changed - Edit, Lua and project layers are separate libraries
- The edit stack (edit plans, script edits, behavior rewrites, the semantic validator, the probe
  analyzer and behavior execution) moves from `src/behavior` and `include/behavior` to `src/edit`
  and `include/edit` and builds into `libnmo_edit`. The Lua runtime and bindings (`src/lua`,
  `include/lua`) build into `libnmo_lua`, and the project authoring layer (`src/project`,
  `include/project`) into `libnmo_project`; both build on `libnmo_edit`. The core `libnmo` no longer
  contains edit, Lua or project code and does not depend on Lua headers. Every function keeps its
  name and signature; consumers link `nmo::edit`, `nmo::lua` or `nmo::project` (pkg-config
  `libnmo-edit`, `libnmo-lua`, `libnmo-project`) in addition to `nmo::nmo`.
- Include paths change: `behavior/nmo_edit_plan.h`, `nmo_edit_plan_json.h`, `nmo_script_edit.h`,
  `nmo_behavior_edit.h`, `nmo_probe_analyzer.h`, `nmo_semantic_validator.h` and
  `nmo_behavior_execute.h` are now under `edit/`. `nmo_behavior_edit_add_link`, `_remove_link` and
  `_mark_interface` are declared in `runtime/nmo_behavior_link_edit.h` (still included by
  `edit/nmo_behavior_edit.h`).
- `nmo.h` no longer includes the edit, Lua and project headers. Use the umbrella headers
  `nmo_edit.h`, `nmo_lua.h` and `nmo_project.h`, or the individual headers.
- `nmo_behavior_execution_lua_runtime()` is declared in `lua/nmo_lua_behavior.h` instead of
  `edit/nmo_behavior_execute.h`, and the runtime is created on first call rather than
  before the action callback runs. It returns NULL if creation fails.
- With `NMO_BUILD_SHARED=ON`, core, edit, Lua and project are still one shared library.
- The built-in schemas share their duplicated hooks: `nmo_object_serialize_staged`,
  `nmo_object_pre_delete_checked`, `nmo_object_post_delete_noop` and the
  `nmo_object_prepare_dependencies_*` hooks replace per-class copies, and the exported
  `<class>_serialize` and `<class>_prepare_dependencies` functions are generated by
  `NMO_DEFINE_OBJECT_STAGED_SERIALIZE[_VALIDATED]` and `NMO_DEFINE_OBJECT_PREPARE_*`.

### Changed - Animation controllers and GUID headers
- `nmo_objanim_controller_t` now describes decoded keys. With `key_count > 0`, `data` holds exactly
  `key_count` keys; with `key_count == 0` it is an opaque blob that is written verbatim (unknown
  controller types, empty controllers, or blobs that do not match the layout of their type).
  In CONTROLLERS files `data_size` no longer includes the 4-byte key count, so it is the size of
  the keys only, as it always was for controllers built by `animation import`, workspace edits and
  project manifests.
- `nmo_objanim_controller_key_size()` returns 0 for the bezier controller types (it returned 44,
  the engine's in-memory key size). Bezier keys are variable-size on disk, so project manifests,
  workspace edits and `animation import` now reject bezier controllers instead of writing keys of
  the wrong size.
- NEWDATA and LEGACY scale-axis keys are 24 bytes (time, an unused float, quaternion), not 20.
  Workspace edits, the project validator and the manifest parser use
  `nmo_objanim_controller_format_key_size()`, so a NEWDATA scale-axis key in a manifest has six
  numbers. CONTROLLERS scale-axis keys stay 20 bytes (five numbers).
- `animation keys` and `animation export` derive the bytes per key from the data, so they no
  longer read past the data of an inconsistent controller, and they decode bezier keys (time,
  position, flags and their tangents). `animation keys` names controller types by type rather than
  by key size, and shows the format's `key_size`.
- `object/nmo_object_guids.h` and `object/nmo_param_guids.h` moved to `type/nmo_object_guids.h` and
  `type/nmo_param_guids.h`, because they only define GUID constants on top of `core/nmo_guid.h`.
  The old headers remain as forwarders.

### Changed - Editing and authoring follow the engine
- Lights: a file whose light type is not 1 to 3 loads as a point light, as `RCKLight::Load` does,
  instead of failing the object. The diffuse alpha is saved as 0xFF like the engine does, so a
  light whose alpha is not 1 can be saved. `VX_LIGHTPARA` is no longer accepted by
  `nmo_entity_edit_set_light_settings()`, `nmo_project_plan_set_light_settings()` or the manifest
  (`light.type: "parallel"`), because the engine cannot store it. Editing a spot light into another
  type resets its cone angles and falloff (`nmo_light_apply_nonspot_defaults()` is now public).
- Materials: new materials get the `RCKMaterial` constructor colors (diffuse 0xFFB2B2B2, ambient
  0xFF4C4C4C, specular 0xFF7F7F7F). `nmo_asset_edit_set_material_render_flags()` turns on the alpha
  blend flag (bit 3) when a blend factor is set and the alpha test flag (bit 4) when an alpha
  function other than always is set, because the engine ignores the blend factors and the alpha
  function otherwise. The alpha function is stored in four bits; the edit masked five.
- Meshes: meshes authored by OBJ import or as the generated cube start with `VXMESH_VISIBLE |
  VXMESH_RENDERCHANNELS` (they had no flags and the engine does not render them) and face channel
  masks of 0xFFFF. OBJ import uses the new `nmo_mesh_update_bounding_volumes()` like the loader.
- `nmo_entity_edit_set_camera_target()` and `nmo_entity_edit_set_light_target()` (and with them the
  project plan and manifest) set `CK_3DENTITY_TARGETCAMERA` / `CK_3DENTITY_TARGETLIGHT` on the new
  target and clear `CK_3DENTITY_FRAME`, and give the previous target `CK_3DENTITY_FRAME` back,
  as `SetTarget` does.

### Changed - Loading accepts what the engine accepts
- A texture's packed state block (`CK_STATESAVE_OLDTEXONLY` / `TEXONLY`) is read as
  `RCKTexture::Load` reads it: flag bits the engine ignores are kept in `packed_unknown_bits` and
  written back, the 12-byte layout reads the video format whether or not flag 0x200 is set (the
  writer then sets it), a block longer than any known layout is read as its fields, and a block
  without a flags dword is skipped.
- A texture's file name list resizes the bitmap slots, as `SetSlotCount` does, instead of failing
  the object when the count differs; added slots are empty.
- A 2D entity of data version 5 or later without the `0x10F000` block keeps the constructor state
  (flags, source rectangle) instead of failing to load, and an older one without `0x4000` keeps the
  constructor flags.
- The sections of every CK2_3D class may be longer than the engine reads; the extra dwords are
  ignored (2D entity, sprite, sprite text, sprite 3D, 3D entity, curve, curve point, place, grid,
  layer, character, body part, kinematic chain, animation, mesh, patch mesh, texture and material).
  A section shorter than the fields still fails as truncated. An animation data block that is not 8
  or 12 bytes is ignored, a patch mesh leaves the vertices, faces, lines and channels to its
  patches, and the camera packs its aspect ratio as `(height << 16) | (width & 0xFFFF)`.
- Camera, light, target camera and target light sections may be longer than the engine reads; the
  extra dwords are ignored as `Load` ignores them. A legacy light's active and specular integers
  mean true for any non-zero value.
- A format 0 layer whose section ends after the header has no square buffer (`Save` writes the
  buffer only when the layer has a grid).

### Changed - workspace_edit.c is split by edit family
- `src/runtime/workspace_edit.c` (5,479 lines) became `workspace_edit.c` (transactions, snapshots,
  journal actions) and one file per family: `_object`, `_param`, `_manager`, `_scene`, `_asset`, `_entity`,
  `_animation`, `_sound`, `_behavior`, plus `_common` and `workspace_edit_internal.h` for what several
  families share. The largest file is 1,310 lines. Nothing else changed: the exported symbols of `libnmo.a`
  are the same, the helpers that stopped being file local gained a `workspace_edit_` prefix, and the whole
  suite passes in the normal, pattern-init and ASan builds.

### Changed - Schema boilerplate shared
- 21 hand-copied staged serialize wrappers use `NMO_DEFINE_OBJECT_STAGED_SERIALIZE[_STATE]`; the file mode
  check (spelled out 29 times, plus six local helpers) is `nmo_object_serialize_is_file` and
  `nmo_object_deserialize_is_file`; seven no-op `pre_delete` and eight no-op `post_delete` functions use the
  shared ones; eight `prepare_dependencies` use `NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE`; seventeen
  `validate` functions that only validated the base state use `NMO_DEFINE_OBJECT_VALIDATE_BASE`. About 1,000
  lines fewer in `src/object/builtin`, no change in the exported symbols or in any test result.
- The generated and the corpus round-trip tests no longer share a scratch file name; ctest ran them in
  parallel in the same directory.

### Changed - More state hooks come from the state layouts
- `nmo_object_state_member_t` gained the kinds `COUNTED` (an arena-owned array counted by an integer member of
  either width) and `RECORDS` (an `nmo_array_t` of records described by a nested member list, with owned
  strings, byte buffers, chunks and counted arrays), and the flag `UNCOMPARED` (copied, but left out of equals
  and hash). The members are now written with designated initializers.
- Create, destroy, copy, equals and hash of the parameter, local parameter, output parameter, synchro, state
  object, critical section, sound, wave sound, midi sound and body part classes, and create and copy of the
  scene, grid, character and behavior object classes (those four compare through the serialized form), come
  from their layouts. 29 of the 43 schemas use layouts now, up from 20; about 1,060 lines fewer in
  `src/object/builtin`.
- Behavior differences, all on states that are not serialized the same way: `equals` of a MIDI sound now
  compares whether its file name came from a file, which decides whether it is written back; the loop word
  of a pre-version-2 wave sound is copied but, as before, not compared; `equals` and `hash` of a synchro
  object no longer return false and 0 for a state that fails validation; the hash values of these classes
  changed, which only the object diff and the hook coverage test read.

### Changed - State layouts cover the entity, geometry and bitmap classes
- Members of a layout can be counted by a function (`NMO_STATE_COUNTED_BY`: three indices per face, the mesh
  weight count that falls back to the vertex count), be counted records (`NMO_STATE_COUNTED_RECORDS`: skin
  bones and vertices, patch channels, material channels, bitmap slots), point at one record
  (`NMO_STATE_RECORD_PTR`: the skin), be optional (`NMO_STATE_COUNTED_*_OPTIONAL`: the unused lanes of a bitmap
  are NULL while the slot count is not), or be custom (`NMO_STATE_CUSTOM`: a value with its own copy, equals and
  hash functions, like the sprite bitmap). A layout needs no base (`base_vtable` is NULL) when it describes a
  plain record, and `nmo_object_layout_hash_from` hashes a layout into a running hash.
- The 3D entity (skin included), mesh, patch mesh, curve, curve point, sprite and texture classes take their
  create, copy, equals and hash (the mesh, patch mesh, curve and curve point compare through the serialized
  form, and only create and copy come from the layout) from layouts, and so do the bitmap slot functions
  `nmo_bitmap_slots_copy`, `_equals` and `_hash`. 35 of the 43 schemas use layouts; with the previous entry
  about 3,000 lines fewer in `src/object/builtin`, with 1,660 added in the engine, the layouts and the tests.
- Hash values of the 3D entity, sprite and texture classes changed; only the object diff reads them.

### Changed - Every schema defines its state hooks from a state layout
- The functions of a custom member (`NMO_STATE_CUSTOM`) get the state that holds the member, so a member that
  depends on a sibling can read it: the cells of a data array row follow the column types, and the buffers of
  the morph sections of an object animation follow the sizes next to them. `equals` and `hash` of a custom
  member may be NULL in a layout that is used for lifecycle and copy only.
- The message manager, attribute manager, interface object manager, level, data array, animation, keyed
  animation, object animation, behavior and object classes use layouts for what was hand written: create,
  copy, equals and hash of the message manager, attribute manager, data array, animation, keyed animation and
  object animation; create and copy of the interface object manager, level and behavior (they compare through
  the serialized form); equals and hash of the object base state. All 43 built-in schemas use layouts now.
  With the previous entries about 4,600 lines fewer in `src/object/builtin` for 2,400 lines added in the
  engine, the layouts and the tests.
- Behavior differences: `equals` of a data array now also compares the parameter type GUID the file held for a
  column and whether it held one, which `serialize` writes back and the hook ignored; the hash values of the
  classes above changed, which only the object diff and the hook coverage test read.

### Changed - Chunk sections described next to the state layout
- A state layout can list the identifier sections of its class (`nmo_object_sections_t`): per section the
  identifier, a presence member, the fields (a dword, an int16 stored as an int, a pair of int16 packed in one
  dword, a reference checked against a class), a phase (the sections of a later phase are read only when none
  of an earlier phase was found) and whether a longer section is accepted. `nmo_object_layout_serialize` and
  `nmo_object_layout_deserialize` write and read the base state and the sections, and
  `NMO_DEFINE_OBJECT_LAYOUT_SERDE` defines the exported entry points of a class. A read decodes into a fresh
  state and replaces the old one only when it succeeded; a save to a chunk without a file writes the sections
  only under the class's save flags.
- CKBehaviorIO, CKBehaviorLink (new layout and the three legacy sections) and CKKinematicChain use it: 341
  lines fewer in their schemas, 350 more in the engine. `test_section_schemas` pins the wire format of the
  three (identifier, offsets, section sizes and the error for a short or a long one, phase order, save flags,
  class mismatch of a reference, atomic failure) and was run against the hand written code first: the same
  eight tests pass on both, and the corpus byte round trip is unchanged.

### Changed - Chunks keep no copy of the bytes they were parsed from
- `nmo_chunk_t` lost `raw_data` and `raw_size`, and `nmo_data_chunk_slice_t` lost `borrowed`. A parsed chunk
  used to keep its source buffer and the data section writer emitted those bytes instead of the chunk, so a
  save could not tell whether the parser and the serializer agreed. They do: the parsed chunks of every
  corpus file (206,928 objects, 1,891 managers) serialize to the bytes they came from, and
  `parsed_chunks_serialize_to_the_bytes_they_came_from` checks the whole data section of each file, on the
  corpus and on generated files. The chunk comparison also covers chunk options and class ids now.
- A chunk written by a manager hook is remapped to file ids like every other chunk; before, one that had been
  parsed from bytes was left alone.

### Changed - Every layer depends only on the layers below it
- The layering audit (`test_layering_audit`) also checks relative includes of another layer's
  private headers (`"../runtime/runtime_internal.h"`); seventeen such upward includes had gone
  unseen. Counting function calls between layers as well, 50 references pointed up; none does now,
  and `tests/layering_allowlist.txt` is empty (38 entries before). `test_layering_calls_audit`
  (`tests/layering_calls_audit.py`, runs when Python is found) keeps it that way: it fails when a
  file names a function with external linkage defined in a higher layer, including the functions
  the `NMO_DEFINE_*` macros define, so an upward call that comes in through a transitive include
  is caught.
- The layer order puts export above object and below session (export uses nothing above object,
  and chunk and document use it).
- Moved to the layer of what they depend on, with the old header paths as forwarders: the context
  to object, the building block registry to extension, the interface chunk type registration and
  the ancestor state lookup to type, the load and save phase timing to format, the document object
  (`nmo_document_create` and its accessors, implemented by the runtime) to runtime, the object
  queries, hierarchy, imports, reference graphs and summaries to runtime, the object diff to
  document, and the load and save pipelines (`nmo_load_file`, `nmo_save_file`, the serializer) to
  session.
- The session is implemented in the session layer (`src/session/session.c`, struct in
  `session_internal.h`); it lived in `src/runtime/document_workspace.c`, so the session code
  called 34 functions of the runtime. The behavior index it caches is built in the behavior layer
  (`behavior_acceleration.c`), which leaves the session the function that releases it.
- The chunk code reads and writes unresolved-reference tokens through `nmo_ref_tokens_t`
  (`format/nmo_chunk_context.h`), which the object repository provides, instead of calling the
  repository.
- Fold and replace-bb rename behaviors through the new `nmo_script_edit_rename_node()` inside their
  script edit transaction; `nmo_behavior_edit_replace_bb_in_script_tx()` is the transaction entry
  point of replace-bb, which the edit plan executor uses. `nmo_behavior_edit_replace_bb_in_edit()`,
  which has only a workspace edit, renames through the same code, so no rewrite renames an object
  directly any more.
- `tools/commands/nmo_cmd_script.c` (4,590 lines) and `nmo_cmd_behavior_interface.c` (3,857) are
  split by command into files of at most 1,754 lines; the functions are unchanged.
- Every edit plan op kind is encoded and decoded from its field table (`src/edit/edit_plan_json.c`,
  2,995 lines before, 2,567 after): the table now also describes an id or a handle of an earlier
  operation, hex bytes, id arrays, fold maps, manager entry options, and fields that are written
  only when one of them was given, so the nine hand-written readers and writers are gone. The
  references to earlier operations are checked from the same tables. Every plan is written as
  before; thirteen invalid inputs that were refused without a message now name the field.
- The Lua wrappers of the edit plan builders whose arguments map one to one onto the builder's
  parameters (11 in `nmo.plan`, 8 on the script edit handle of `nmo.behavior`) are generated by
  `tools/scripts/gen_lua_edit_bindings.py` into `src/lua/*_ops.generated.inc`, which the binding
  files include. The generator reads the builder prototypes from `edit/nmo_edit_plan.h` and stops
  when a spec no longer matches one; `test_lua_edit_bindings_generated` fails when the generated
  files are out of date. The Lua functions behave as before.

### Removed - Unused APIs (July to September)
- Headers: `session/nmo_builder.h` (the staged file builder), `session/nmo_runtime_result.h` (result
  objects of copy, destroy and destroy preview that only the Lua bindings used),
  `object/nmo_object_iter.h`, `object/nmo_value_writer.h`, `lua/nmo_lua_value.h`,
  `extension/nmo_extension_diagnostics.h`, `io/nmo_io_checksum.h`, `io/nmo_io_compressed.h`,
  `core/nmo_hash_set.h`, `core/nmo_list.h` and `core/nmo_pool.h`.
- `format/nmo_chunk_parser.h` and `format/nmo_chunk_writer.h`: the parser and writer objects were a
  second implementation next to the chunk. `format/nmo_chunk_api.h` gained what only they offered
  (reserve and patch, 16-bit little-endian buffers, dwords as words, buffer locks,
  `nmo_chunk_seek_identifier_with_size()`).
- The `nmo_io_file_*` and `nmo_io_memory_*` functions (the `nmo_io_interface_t` API stays), the view
  builders `nmo_comparison_build_view()`, `nmo_diff_build_view()` and `nmo_object_summary_build_view()`
  with their `_destroy` and `_collect_stats` companions, `nmo_chunk_inspect_validate()` and
  `nmo_deserialize_store_remaining()`.

### Removed - The save_buffer module and internal helpers
- The unused `save_buffer` module and its forward typedef `nmo_save_buffer_t`, and internal helpers
  with no callers.

### Removed - BeObject attribute clone helpers
- `nmo_beobject_clone_attributes` and `nmo_beobject_clone_legacy_attributes` are gone. They deep copied the modern
  and the legacy attribute arrays of a CKBeObject for the hand written copy hook; the state layout of the
  behavior object does that now, and nothing else called them.

### Fixed - Malformed input fails cleanly (July to September)
- Reading and writing the state of a built-in class is atomic: a state is replaced only when its
  chunk was read completely, and a failed write leaves no partial chunk behind.
- Seek and read errors reach the caller instead of being ignored, counts are checked before anything
  is allocated, and payloads are bounded to their identifier section, so a corrupt count or offset
  fails instead of reading into the next section. The chunk reader and writer reject unterminated
  strings, sizes the format cannot encode and overflowing arithmetic, and chunks, Header1 and the data
  section are parsed into a staging state that is published only on success.
- Failures in the load pipeline (reference resolver, remap tables, shadow storage, included files,
  finalization) are reported instead of continuing with a partial result.
- Core fixes: large aligned arena allocations, string mutations whose source overlaps the
  destination, removal from containers that own their elements, object renames and index updates
  that fail half way, and the durability of transactional file writes.

### Fixed - Saves keep chunk metadata (July to September)
- A save keeps the class id of each chunk and the plugin dependencies of the file, a targeted save
  or a reference normalization leaves the chunks of the other objects untouched, and the write mode
  flags of the header are the ones Virtools writes. `diff` reports differences in chunk metadata and
  plugin dependencies.

### Fixed - CKObjectAnimation controller keys
- CONTROLLERS-format controllers are stored as `[u32 key_count][keys]` (checked against the
  engine in CK2_3D.dll). libnmo read the whole blob, set `key_count` to 0 and kept the count in
  `data`, so `animation keys` and `animation export` showed no keys for any real file. Reading now
  strips the count and fills `key_count`; writing adds it back. Every CONTROLLERS animation in
  `data/` reserializes byte for byte.
- Animations authored by `animation import`, workspace edits and project manifests in the
  CONTROLLERS format were written without the key count, which the engine would read as the
  first key's time. They now carry it. Files written by earlier versions lack it and, in
  practice, are read as opaque blobs.

### Fixed - Chunks survive save and reload byte for byte
Found by the new `test_corpus_chunk_roundtrip`, which saves every object of `data/` and compares
each chunk with the original (payload, id list, sub-chunk offsets, manager list and sub-chunks).
Before these fixes about 7% of the objects came back different.
- Sub-chunks keep the file flag stored in their header. The writer set it on every sub-chunk whose
  parent had a file context, while `CKStateChunk::WriteSubChunk` writes whether the sub-chunk
  itself had one.
- Object ids inside sub-chunks whose file flag is 0 are no longer remapped in file chunks; they
  are not file indices, and remapping them on load and save shifted every such id by one.
- A behavior keeps the runtime bits of its flags (active, executed last frame and the `*NEXTFRAME`
  bits) across save. They are kept in the new `nmo_behavior_state_t.runtime_flags`; `flags` is
  unchanged.
- Manager-mode parameters record their position in the chunk's manager list.
- A texture reads the packed state (mip level, save options, transparent color, video format)
  stored under `CK_STATESAVE_TEXONLY` (0xFFF000) as well as `CK_STATESAVE_OLDTEXONLY`, and writes
  back the identifier the file used (`nmo_texture_state_t.uses_texonly_identifier`). Such textures
  previously lost those values.
- A mesh whose file stores an all-zero normals block writes it back instead of omitting normals
  (`nmo_mesh_state_t.zero_normals_stored`).
- Null strings in data arrays are written as null rather than as empty strings.
- `texture show` reads the save options as the `CK_TEXTURE_SAVEOPTIONS` enumeration instead of a
  mask: value 3 is now reported as `use_global` (it was `external`) and 4 as `include_original`
  (it was `use_global`). `NMO_CKTEXTURE_USEGLOBAL` and `NMO_CKTEXTURE_INCLUDEORIGINALFILE` were
  4 and 8 and are now 3 and 4, matching the enumeration. `is_external` is unchanged.
- Known gap, not written back and excluded from the test: CKPatchMesh section 0x8000, the face
  masks of the built render mesh. The engine writes it but ignores it on load, so it is derived
  data.

### Fixed - Schemas read fields as the engine defines them
An audit of the CK2_3D classes against the engine (CK2_3D.dll, CK2.dll) found fields that were
read into the wrong member or given the wrong meaning. The bytes of these fields always
round-tripped, which is why the byte-level test did not see them.
- Skin vertices: the first per-vertex buffer holds the bone weights (floats that sum to one) and
  the second the bone indices. They were read the other way round. In `nmo_3dentity_skin_vertex_t`
  the members now follow the file order (`bone_weights`, then `bone_indices`, and the two
  `legacy_before_*` words swap places). Checked on all 28,572 skinned vertices of the corpus.
- Curve points: `nmo_curvepoint_state_t.use_tcb` is now `tangent_mode`. The file value is 0 for TCB
  points and 1 for points with explicit tangents, the opposite of what the old name said.
- Patch mesh channels: the three dwords after each texture patch id are `source_blend`,
  `dest_blend` and `flags` (they were `flags`, `type`, `subtype`). Entry 0 of the channel list is
  the base texture-coordinate set and its dwords are uninitialised in the files.
- Raw texture planes are stored bottom-up: `nmo_image_reconstruct_pixels()` now returns top-down
  pixels, so `texture extract` no longer writes raw textures upside down.
- `nmo_chunk_write_raw_bitmap()` / `nmo_chunk_read_raw_bitmap()` order the planes like the bytes of
  a pixel (blue, green, red, alpha). They used red, green, blue and swapped red and blue against
  every real file.
- `texture extract` applies the constant alpha of a reader slot that stores one distinct alpha
  instead of a plane, so fully transparent textures no longer come out opaque.
- The mesh bounding sphere is centered on the mean of the vertices, as `UpdateBoundingVolumes`
  does, not on the center of the bounding box (`bary_center`, `radius`; derived values).
- `mesh export` writes the normals CK rebuilds for meshes whose file omits them, instead of
  lines of `vn 0 0 0`. New: `nmo_mesh_normals_are_derived()` and `nmo_mesh_build_vertex_normals()`
  in `object/builtin/nmo_mesh_schemas.h`. The state and the bytes written are unchanged.
- Sprites keep the video format section (`CK_STATESAVE_SPRITEVIDEOFORMAT`, 0x40000000) that later
  engines write: `nmo_sprite_state_t.has_video_format` and `video_format`. Saving used to drop it.

### Fixed - Files older than the corpus
No file in `data/` is older than data version 9 (meshes) or 10 (most classes), so these layouts had
been written from assumptions. They now follow the engine's `Load` functions (CK2_3D.dll, CK2.dll);
the tests build each layout dword by dword.
- Meshes before data version 9 are read as `RCKMesh::Load` reads them. Vertices: from version 5 a
  save flags dword and an unframed block (positions, then normals for a lit mesh or diffuse and
  specular for a `VXMESH_PRELITMODE` mesh, then UVs), versions 1 to 4 one record per vertex, version 0
  framed vectors. Faces come as a list of material groups, each a material and its faces (version 0:
  a record per face), and the mesh's material groups are built from them as `SetFaceMaterial` does.
  Lines are the framed buffer from version 1 and two integers each in version 0. Saving writes the
  current layout and data version, as the engine does, so a mesh loaded from an older file is saved
  as a current one.
- Materials before data version 5 store each color as a size-prefixed buffer
  (`[16][r][g][b][a]`), 33 dwords in all; libnmo read and wrote 29.
- Textures before data version 5 keep the mipmap flag and image descriptor under
  `CK_STATESAVE_TEXVIDEOFORMAT` (0x40000) and the save options under `CK_STATESAVE_TEXSAVEFORMAT`
  (0x80000), not under 0x400000 and 0x800000. The state fields are renamed to match:
  `has_legacy_video_format`, `legacy_video_format_data` / `_size` and `has_legacy_save_format`.
  The pick threshold is read only from data version 5, and a reader slot whose alpha count is not 1
  is followed by an alpha plane.
- Body parts before data version 5 store the rotation joint as a size-prefixed block, and the joint
  flags follow the engine's shift of `axis - 1` (axis 0 of the first vector sets bit 31 and axis 0 of
  the other two sets nothing). A legacy block is refused for flags it cannot read back.
- Object animations take the legacy path first for data version 0, where identifier 0x1000 is the
  three-float root vector instead of the new-data section.
- 2D entities read the material section only from data version 5.
- `nmo_texture_replace_bitmap()` gives the stored PNG the PNG reader's extension and GUID, so the
  engine can find a decoder for it.

### Fixed - Small engine mismatches
- Sprite text files store the font integers as underline, italic, weight, size (the order
  `RCKSpriteText::Load` hands them to `SetFont` in reverse); libnmo read them as size first.
- A legacy bitmap2 texture slot starts with a five-byte format tag (`CKTGA`, `CKJPG`, `CKDIB`,
  `CKBMP`, `CKTIF`, `CKGIF`, `CKPCX`) that selects the reader. `nmo_texture_bitmap2_image()` strips
  it and `texture extract` uses it; the slot's leading integer is named `unused_int`.
- `texture extract` no longer reads the raw slot `compression` as a DXT type. The engine keeps its
  low four bits: 0 stores the planes as they are, 1 uses Virtools' own DCT codec, anything else
  stores no colour planes. Only 0 is decoded.
- A new texture starts with `CKTEXTURE_USEGLOBAL` and the packed state block, as a new `RCKTexture`
  does, so `CKTEXTURE_RAWDATA` is written instead of being read back as use-global.
- `texture show` calls a texture external only when it embeds no bitmap, and gives the dimensions
  and bits per pixel of a raw texture.
- Morph controllers in the CONTROLLERS format: `nmo_objanim_morph_controller_info()` and
  `nmo_objanim_morph_controller_key()` read the blob (key count, vertex count, has-normals, then a
  time, positions and compressed normals per key); `animation show` and `animation keys` list them.
- Camera: `width` and `height` are `aspect_width` and `aspect_height`; `CK_CAMERA_PROJECTION`
  names the projection (the engine tests the low bit); `entity show` prints the aspect ratio and the
  orthographic zoom. Editing the camera settings of a camera without a CAMERAONLY section now
  writes the section.
- Light: `NMO_LIGHT_FLAG_ACTIVE` and `NMO_LIGHT_FLAG_SPECULAR` name the flag bits; `entity show`
  prints them and the spot cones and falloff instead of the specular and ambient colors, which the
  engine computes at runtime and never stores.
- Layers of version 2 associate `CKPGUID_INT` with their type, as the engine does, and a new layer
  defaults to it.
- `CK_OBJECTANIMATION_TAG0` and `_TAG1` name the two flag bits the corpus sets and the engine
  never tests. The texture header lists the identifiers of the engine and the bitmap data flags
  with their real values.
- Raw bitmaps with compression 1 store each colour plane in Virtools' own DCT codec (not JPEG).
  `nmo_image_decode_dct_plane()` decodes it; `nmo_chunk_read_raw_bitmap()` and `texture extract` use
  it. No file in the corpus has it and the engine's `Save` never writes it.
- `nmo_chunk_write_encoded_bitmap()` and `nmo_chunk_read_encoded_bitmap()` use the layout of
  `CKStateChunk::WriteReaderBitmap` and `ReadReaderBitmap` (kind, extension, reader GUID, image,
  and for a codec without alpha the distinct alpha count with the value or the alpha plane); they
  stored their own layout before.
- A new place starts with priority 20000 and a new grid with the scale (1, 10, 1), as the
  constructors set them.

### Fixed - Differences the second audit found
- New CK3dEntity objects start with an identity matrix and the moveable flags the engine constructor
  sets (0x4000B), and new body parts with joint flags 7; they started zeroed.
- `nmo_character_effective_root_body_part()` gives the root body part the engine works with: the
  stored one, or the character's first child when none is stored.
- A NEWDATA animation keeps its MORPHCOMP and MORPHNORMALS sections independently
  (`morph_comp_*` and `morph_normals_*`; `morph_normals_id` is gone). Both are ignored when there
  are no morph keys, as `Load` does.
- Deleting the owner of shared animation keys moves the keys into the animations that shared them,
  and an animation whose owner is unresolved saves as an empty CONTROLLERS animation of length 100,
  as the engine's reference counted keyframe data behaves.
- The mesh serializer refuses face vertex indices and material groups outside the mesh and channels
  with more texture coordinates than vertices, which the engine reads without a check, and it no
  longer writes an empty material list.
- A sprite's bitmap is modelled as a texture's is: `nmo_bitmap_slots_t` holds the reader, raw and
  obsolete slots, the slot file names and the movie file name, under the identifiers RCKSprite
  passes to `CKBitmapData`. They were five unlabelled raw payloads.
- Importing an OBJ into a mesh keeps the scripts, attributes, priority and visibility of the mesh
  object; it replaced all of its state.
- Loading follows `Load` more closely: a 3D entity ignores the parent, flags and matrix sections
  when it has NDATA, reads a skin's normals as 12 bytes per vertex
  (the count-prefixed variant `normals_have_count` is gone) and writes no mesh section for a curve;
  a material takes longer sections and applies MATDATA3 and then MATDATA5; a curve without its
  sections keeps 100 steps and stays open.
- New 2D entities, sprites and sprite texts start with the constructor flags and source rectangle of
  the engine, and a new object animation is 100 frames long.
- A format 0 layer is written with its square buffer only when it has a grid.

### Fixed - A save keeps what the file held
- A default save used to re-serialize every loaded object through its schema, so anything the schema
  does not model (trailing dwords of a section, sections of another version, the padding Virtools
  leaves behind strings) was lost even for objects nobody touched. A load now remembers a digest of
  each object's state; on save an unchanged object keeps its original chunk (its object ids are
  translated when other objects were deleted or moved), and an edited one is written by its schema
  and then gets back what the file held beyond it: sections the schema does not know, trailing
  dwords, and values the schema normalized that the edit did not touch. The corpus now saves
  byte for byte, padding included. The pieces are `nmo_chunk_digest`, `nmo_chunk_equivalent`,
  `nmo_chunk_translate_ids_with_layout` and `nmo_chunk_merge_residue`.
- The level scene sub-chunk of a CKLevel (a CKScene-format chunk written with a file, so its object
  ids are plain file indices) is read as a scene to find where its ids are; they are then held as
  runtime ids and written as file indices, so the level scene follows the objects when indices
  change (a subset export or a deleted object used to leave stale indices). An object that is gone
  is written as -1.
- A save keeps its fidelity state in step: a chunk the save replaces without committing it (a
  save of a subset, a failed save) no longer leaves the old digest behind, a manager chunk whose
  manager took it at load but writes none now is kept, and pieces of the original data that could
  not be carried over are logged.
- Edited objects keep more of what the file held: section tails survive in chunks that hold
  sub-chunks or manager ids, a light keeps the type byte and diffuse alpha the file holds while the
  field is unchanged, a character keeps a SAVEPARTS section, a midi sound keeps a MIDISOUNDFILE name
  read from a file, a data array column keeps the old time GUID, ParameterIn and ParameterOperation
  keep the type GUID of the file next to the mapped one, and an object animation keeps the four
  floats the engine ignores and its sections before OBJECTHIDDEN. A state the schema refuses to
  write (stricter than the engine's checks) keeps the chunk it was loaded with. A kinematic chain
  reads its first dword as an object reference.
- Following the engine: ParameterIn's second reference of the DEFAULTDATA layout is the shared
  source (they were swapped), legacy building blocks keep their parameter and IO sections, an
  object animation without a keyframe section has length 100, a raw bitmap of any non-zero bit depth
  decodes, the buffered mesh weights form is decided by section size, and object ids and object
  arrays of chunks older than version 4 are read in their old encoding.
- `nmo_murmur3_32` read past the end of its data (the block loop started at the end), so the hashes
  of arrays and strings depended on other memory; it now matches the reference vectors. Object
  diffs print counted arrays, state chunks and opaque pointers by content instead of by address, so
  `diff objects` gives the same output on every run (apart from the JSON timestamp).
- The snapshot bitmap of a parsed interface chunk is written back as the file held it while its
  pixels are unchanged; encoding the pixels again gave a different BMP header, so every parsed
  interface with a snapshot differed from its chunk. All 2066 parsed interfaces of the corpus now
  write back equivalent to their chunk (`test_corpus_chunk_roundtrip`).
- The interface chunk of a behavior that is not a script (a graph below a script) is parsed:
  its root has the header of a sub-behavior (sizes instead of the start position, no snapshot or
  color) and carries graph input/output tables, and a sub-behavior whose id the file no longer has
  may lack its parameter section. All 183 such chunks of the corpus parse and write back
  equivalent (they used to be kept raw), and `nmo_interface_parse_ctx_t` gained `is_script` and
  `is_known` callbacks for it.
- Enum and flags types that the library registers itself now derive from Integer as in the
  engine's type table (the table was skipped for types that already existed), so operation
  signature matching sees an enum pin as an Integer.
- Chunks older than version 4 read the old sub-chunk layout (and newer chunks detect it as the
  engine does). The data section of a file below version 8 starts with the highest file id and
  the object count and is read and written that way; files of version 7 can be loaded.
- File version below 8: the CRC is the Adler32 of the data section as `CKFile` checks it
  (`nmo_file_crc_for_version`, `nmo_file_header_verify_crc`), the manager block is not written
  below version 6, and a file below version 7 is refused at load (its object table is in the
  chunks, which is not read).
- Smaller engine differences found by the third audit: a new mesh starts visible with render
  channels, the odd face channel mask word goes to the last face, a controller without keys is
  written as `{type, 1, 0}`, a sprite text load keeps the ratio offset flag of the file, sound
  sections accept trailing dwords and a wave file name is written whole, a texture without a packed
  state block defaults to global save options, replacing a bitmap replaces the whole image, a
  layer edit is written in a newer layout when the loaded one cannot hold it, a character reads its
  four references whatever the count says, message type names may be null, and the behavior array
  flags follow the arrays.
- The 3D entity z-order and the entity matrix of a legacy curve point are kept as stored; the values
  the engine uses come from `nmo_3dentity_effective_z_order` and `nmo_curvepoint_get_position`.

### Fixed - Rolling back an edit restores the interface of a behavior
- The snapshot a script edit takes of a behavior copied the struct, which shares the `interface_data`
  pointer, so changes made to the editor layout in place survived `nmo_script_edit_rollback` while the graph
  arrays came back. After `remove_node` and the canonicalize policy the interface held one sub less than the
  graph and a rollback left it that way (`rollback_restores_the_interface_the_policy_canonicalized`). The
  snapshot now deep-copies the interface data (`nmo_interface_data_copy`, moved to the format layer and
  checked against all 2,304 interfaces of the corpus) into the journal and a rollback copies it back.
- `nmo_interface_graph_io_set_array` takes the matching tag array and clears it. It used to leave the tags
  of the old ports behind the new array, so a longer port list made the writer read tags past the end. The
  interface data copy accepts ports without tags.
- `nmo_script_edit_open_interface` and `nmo_script_edit_interface_changed` are the gateway for in-place edits
  of the editor layout, and every `nmo behavior interface` command runs inside a script edit transaction. The
  commands used to change the layout first and open a separate edit only to mark it, so a failed or dry-run
  command could not be undone.

### Fixed - Copying a behavior dropped its runtime flags
- The copy hook of a behavior did not carry `runtime_flags` (active, executed last frame, activate or reset
  next frame), so a copied behavior was saved with those bits cleared and did not equal its original. It was
  found by `test_corpus_state_copy`, which copies the state of every object of the corpus into a zeroed and a
  created state and checks validate, equals, hash and the serialized dwords of the copy, and destroys the
  copy before the original so shared memory shows under the sanitizers. 2,723 behaviors of the corpus were
  affected.

### Fixed - The object name index kept freed names as keys
- An entry of the name index (`nmo_object_index_t`) used the name storage of the first object with that
  name as its key. When that object was taken out of the repository, deleted or renamed while another
  object still had the name, the key pointed at freed memory, so the next lookup of the name read freed
  memory or missed the entry. Each entry now keeps its own copy of the name. Virtools files often give
  several objects the same name, so deleting or renaming objects of a session with an object index hit it.

### Fixed - The examples build and run
- `simple_save`, `file_converter` and `custom_manager` used the session functions that `nmo.h` no
  longer declares and did not compile; they use the document API now. `simple_save` creates a camera
  through a workspace edit before it saves, since an empty document cannot be saved.
  `custom_manager` destroyed the manager it had handed to the manager registry, which destroys it
  again with the context. `file_converter --validate` no longer turns on the plugin dependency check,
  which fails whenever the context does not know the plugins of the file; `--check-dependencies`
  does that.
- `object_type_usage` and `type_system_example` were not in `examples/CMakeLists.txt`. They are built
  now, with the current GUID and state names; `type_system_example.c` had a copy of an example for
  the removed type system v2 prototype appended to it, which is gone.
- With `NMO_BUILD_EXAMPLES` and `NMO_BUILD_TESTS` on, CTest runs the examples that need no sample file
  (save, convert, load and inspect a file, and the registration examples). The sanitizer job of CI
  builds and runs them.

### Tests
- Six corpus tests (`test_corpus_semantics_{geometry,media,scene,behavior}` and the earlier
  `test_corpus_invariants`) check about 300 relationships the engine guarantees between decoded
  values; each invariant must be checked at least once, and mutating a read in a scratch copy of
  the schema makes them fail.
- `test_corpus_chunk_roundtrip` checks every object chunk of the corpus. It compares against a
  second load of the original file that is never saved. It tolerates only the uninitialised
  padding bytes Virtools leaves behind strings and buffers.
- The corpus directory walk moved into the test framework (`test_corpus_walk`).
- `test_generated_chunk_roundtrip` runs the same checks on files the library generates, so CI covers
  them without the Virtools samples: one object of every class, a project-authored level with a script,
  and copies of the first with data the schemas do not model (a trailing dword, a section nobody knows).
  It checks the round trip, a default save byte for byte, deleting an object, and that editing one object
  keeps that data and changes nothing else. The comparison moved to `chunk_roundtrip_check.h`; the deletion
  check now also compares what every object points at, by class and name, which the corpus test passed
  even when a moved object's index was left stale (skipping the id translation is caught now).
- `test_corpus_chunk_roundtrip` also saves the corpus with the default options and requires every
  chunk to come back byte for byte, and deletes an object to check that the others keep their data.
  `test_fidelity_save` edits a material that carries an extra dword and checks the dword survives.
- Three interface chunk tests did not clear their parse context, so the `is_script` and `is_known`
  callbacks were read from the stack. The Linux CI build crashed on it; macOS happened to hand out
  zeros. They clear the context now.
- The corpus tests no longer link `m` by name (Windows has no such library); `nmo` already links it
  where it exists.
- The level scene probe allocates its scene state from the arena. GCC on MinGW reported a false
  array-bounds error for the stack object.
- CI has a `ubuntu-clang-pattern-init` job that builds with `-ftrivial-auto-var-init=pattern` and
  runs the suite, so a struct used without being cleared fails on every platform. A local scan
  with the same flag, heap scribbling and the clang static analyzer found no other case, and the
  CLI output over the corpus is identical with and without the flag.
- CI also has a `ubuntu-clang-msan` job (MemorySanitizer with origin tracking). It found that the type
  registry left `element_size` unwritten in field definitions it builds in the arena
  (`nmo_type_registry_add_field`, `nmo_type_registry_register_struct_string`), which
  `register_struct` and `finalize_struct` then read. Both clear the definition now. The corpus is not
  in CI, so this job runs the tests that need no corpus files.
- Tests no longer write into the source tree. The framework creates a scratch directory in the build
  tree (`NMO_TEST_SCRATCH_DIR`, `<build>/tests/scratch`, with `NMO_TEST_SCRATCH_FILE(name)`), and the
  tests that wrote elsewhere use it: `test_io_mmap` wrote into `data/`, and `test_behavior_execute`,
  `test_load_options` and `test_virtools_types`, which run with the source root as their working
  directory, wrote there (`test_behavior_execute` left six `.cmo` files behind on every run).
- Six tests named their samples relative to the working directory, which is the build directory under
  CTest, so they never found them: `test_bulk_destroy`, `test_strict_load` and four cases of
  `test_interface_chunk` skipped even with the corpus present, and `test_file_roundtrip` and
  `test_mmap_load` printed "skipped" and returned 0, so CTest counted them as passed. They use
  `NMO_TEST_DATA_FILE()` and `TEST_REQUIRE_FIXTURE()`, a sample that does not load fails the case, and
  the two integration tests load samples that exist (the Nop files, and the two uncompressed samples for
  the mmap path). Running them found the name index bug above.

## [0.2.0] - 2026-07-14

### Added
- Lossless `nmo_ref_t` references with explicit resolved, unresolved, ambiguous,
  class-mismatch, and null states.
- Caller-owned load diagnostics and strict end-of-scan validation for recoverable
  schema failures.
- Explicit invalid-reference normalization for save-as workflows.
- Sectioned InterfaceChunk support for local/shared parameter sections and both
  graph mapping section pairs, including raw mapping tags.

### Changed
- All 43 built-in schema implementations propagate chunk read, skip, and write
  failures; a source audit test prevents ignored results from returning.
- Failed schema state is discarded while its original chunk remains available
  for an unchanged save.
- Unresolved IDs are preserved through legacy schema load/save paths instead of
  being silently encoded as null references.
- Grid layer ID/chunk lanes are represented as atomic `nmo_grid_layer_t` records.
- Behavior graph traversal detects cycles, and Behavior index rebuilds release
  their previous slot storage.

### Fixed
- Negative and impossible sequence/manager counts are rejected before allocation.
- Behavior dependency remapping no longer compacts serialized lanes.
- Sectioned InterfaceChunk writing preserves absent versus present-empty sections.

The entries below were recorded as unreleased work after 1.4.0 of the earlier numbering and
shipped in 0.2.0.

### Added - Phase 8: Round-Trip Framework
- DOM comparison API (`nmo_comparison.h`): diff two loaded sessions at the object
  level; used by round-trip integration tests
- Round-trip test framework: loads a file, saves it to a memory buffer, reloads
  from buffer, and runs DOM comparison
- IntList auditor: debug-mode verifier that records every `StartIntList` write and
  asserts the correct count on `StopIntList`

### Added - Phase 8: Dual-Track IO and Reserve-and-Patch
- Dual-track IO: automatically selects mmap (zero-copy) or buffered file IO based
  on file size and OS capabilities
- Reserve-and-patch pattern: `nmo_chunk_reserve_dword()` writes a placeholder and
  `nmo_chunk_patch_dword()` fills in the real value after the size is known
- Transactional write (`nmo_txn`): platform-specific atomic-commit helpers
  (POSIX `fsync` / Windows `FlushFileBuffers`)

### Added - Phase 7: Save Pipeline and Core Infrastructure
- Two-phase commit save pipeline:
  - Phase 1 (Layout and Serialize): all objects serialized into memory chunks;
    ID mapping computed; shadow blobs restored
  - Phase 2 (Pack and Commit): file header written; optional zlib compression;
    CRC-32 appended; atomic fsync
- Chunk writer version context stack: 16-level nesting; parent version propagated
  automatically on push/pop
- ID sanitizer (`nmo_id_sanitizer_t`): strips `0x800000` reference marker,
  handles negative external-reference IDs, maintains bidirectional
  file-index <-> runtime-ID mapping; 6 unit tests
- Shadow storage (`nmo_shadow_storage_t`): retains included-files blob and raw
  chunk tail bytes so unknown data survives round-trips; 10 unit tests

### Changed
- Object layer: all 23 CK class schemas + 2 manager schemas migrated to
  explicit vtable dispatch.  No legacy bridge macros remain.
  `RuntimeFallback = none` for all registered types.
- Test count reached 102/102 (up from 88 at 1.4.0)

---

## [1.4.0] - 2025-12-20 - Phase 6 Completion (Type System)

### Added - Type System
- Type registry (`include/type/nmo_type_system.h`):
  - GUID-first type identification with O(1) hash lookups
  - Support for primitives, enums, flags, structs, and manager types
  - Type inheritance via parent GUID chaining
  - UI visibility flags and type categories
  - Arena-based allocation; slot recycling; Tortoise-Hare cycle detection
- Enum/flags registration:
  - `nmo_type_registry_register_enum()` -- named value enumerations
  - `nmo_type_registry_register_flags()` -- bitfield flag types
  - Value-to-name and name-to-value conversion APIs
  - Combined flags string (e.g. `FLAG_A|FLAG_B`)
- Operation registry (`include/type/nmo_operations.h`):
  - 4D dispatch tree: Operation -> P1 type -> P2 type -> result type
  - 50+ builtin operations: arithmetic, logic, bitwise, trig, vector
- String conversion (`include/type/nmo_type_string.h`):
  - `nmo_type_to_string()` / `nmo_type_from_string()` generic conversion
  - Built-in formatters for primitives, vectors, colors
- Manager type descriptors:
  - `nmo_manager_type_descriptor_t` for custom manager serialization
  - Serialize/deserialize callbacks integrated with the chunk API
- Reflection (`include/type/nmo_reflection.h`): struct field introspection

### Added - Object Layer (Phase 6.1)
- Object index system (`include/object/nmo_object_index.h`):
  - O(1) lookup by class ID, name, or GUID (incremental, add/remove)
  - Index rebuild and statistics APIs
- Object repository (`include/object/nmo_object_repository.h`):
  - Dual-index: `nmo_indexed_map_t` + name hash table
  - Move-semantics `add` (sets `*obj_ref = NULL` on success)
  - Runtime ID allocation with wraparound

### Added - Extension Layer
- Extension registry (`include/extension/nmo_extension_registry.h`):
  owns all plugins; supports static and DLL-based registration
- Extension host ABI (`include/extension/nmo_extension_host.h`)
- Extension diagnostics (`include/extension/nmo_extension_diagnostics.h`)
- Extension loader (`include/extension/nmo_extension_loader.h`):
  loads `.dll`/`.so` plugins; enforces unregister-before-unload rule

### Changed
- `nmo_context_t` now uses `nmo_type_registry_t` internally
- Deprecated `nmo_schema_registry_t` (Builder-pattern API); see MIGRATION_GUIDE_V2.md

### Fixed
- Context initialization: type registry properly created in `nmo_context_create()`
- Removed tests for non-existent data files (`Empty.cmo`, `Empty.vmo`)

### Tests
- 88/88 tests passing at release time

---

## [1.3.1] - 2025-12-05 - Bug Fixes and Streaming IO

### Fixed
- vtable write functions missing `arena` parameter (P0 issue):
  - Added `arena` to all 23 vtable write function signatures
  - Removed 4 NULL-arena workarounds in mesh, light, camera, 3d-entity schemas
  - Updated `nmo_schema_write_struct()` to accept the arena parameter

### Added
- Session-level chunk pool wiring: chunks allocated during load reuse
  `nmo_chunk_pool_t`, improving memory locality
- `nmo_data_section_parse()` accepts an optional chunk pool parameter
- Chunk compression APIs: `nmo_chunk_compress()`,
  `nmo_chunk_compress_if_beneficial()`, `nmo_chunk_decompress()`
  (replace older pack/unpack helpers; backward compatible)
- Streaming IO subsystem (`include/io/nmo_io_stream.h`): incremental
  reader/writer for large data sections; configurable buffer sizes;
  transparent (de)compression; streaming writer patches file header on finalize
- `nmo_string_t` -- dynamic string container + `nmo_string_view_t` helpers
  providing XString-compatible behaviors (assign, append/insert, replace,
  search, case conversion, printf-style formatting, numeric conversions)

### Tests
- `test_data_roundtrip`: added `parse_with_chunk_pool` case
- `test_chunk_api`: extended with compression API coverage
- `test_stream_io`: round-trip streaming save/load (compressed and uncompressed)
- `test_string`: full string API coverage

---

## [1.3.0] - 2025-11-12 - Utility Refactoring and Enhanced Chunk Features

### Added - Core Utility Library
- `nmo_utils.h` -- unified utility library:
  - Alignment: `nmo_align_dword()`, `nmo_align()`, `nmo_bytes_to_dwords()`
  - Byte order: `nmo_bswap16/32/64()`, `nmo_le*toh()`, `nmo_htole*()`
  - Little-endian read/write: `nmo_read_u*_le()`, `nmo_write_u*_le()`
  - Min/max/clamp: `NMO_MIN`, `NMO_MAX`, `nmo_clamp_*()`
  - Buffer bounds: `nmo_check_buffer_bounds()`, `NMO_CHECK_BUFFER_SIZE`

### Added - Enhanced Chunk Features
- True 16-bit endian conversion (real byte swap, not just aliases):
  - `nmo_chunk_parser_read_array_lendian16()` with real word swap
  - `nmo_chunk_writer_write_array_lendian16()` with real word swap
- Complete math type read/write (Vector2, Vector3, Vector4, Matrix4x4,
  Quaternion, Color) -- all verified with round-trip tests
- Deep chunk cloning in `nmo_chunk_clone()`: recursive sub-chunk copy,
  independent data/ID/manager buffer copies
- `nmo_chunk_parser_seek_identifier_with_size()`: returns size until next
  identifier (matches `CKStateChunk::SeekIdentifierAndReturnSize()`)

### Changed
- Refactored 7+ source files to use `nmo_utils.h` instead of local duplicates
- Simplified `chunk_internal.h` to a thin wrapper

### Tests
- `tests/unit/test_chunk_advanced.c`: 9/9 tests passing

---

## [1.2.0] - 2025-11-12 - Phase 5: Object Indexing and Performance

### Added - Object Indexing System
- `include/object/nmo_object_index.h`: class-ID, name, and GUID indexes for
  O(1) lookup; incremental add/remove; index rebuild; statistics API

### Added - Hash Table and Arena Enhancements
- `nmo_hash_table_reserve()` / `nmo_hash_table_get_capacity()` for
  pre-allocation; power-of-2 capacity rounding
- `nmo_arena_config_t` + `nmo_arena_create_ex()`: configurable block size,
  growth factor, alignment
- `nmo_arena_reserve()` for bulk pre-allocation

### Performance
- Object lookup by class:  50-100x faster (O(n) -> O(1))
- Object lookup by name:   100-200x faster (O(n) -> O(1))
- Object lookup by GUID:   50-150x faster (O(n) -> O(1))
- Arena allocation:        5-10x faster
- Hash table bulk insert:  30-50% faster with reserve
- Memory overhead:         20-30% for 50-200x performance gain

### Tests
- 7/7 unit tests passing
