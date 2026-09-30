# Public API Tiers

Every public header in `include/` declares which tier its API families belong
to. The tier tells a caller how much the API is expected to change and whether
it is suitable for long-lived code or language bindings.

The tiers are defined in `include/nmo_types.h`:

| Tier | Enum | Intended callers |
| --- | --- | --- |
| 1 | `NMO_API_TIER_STABLE_CONSUMER` | Applications, scripts, and language bindings |
| 2 | `NMO_API_TIER_ADVANCED_C` | C tooling that orchestrates lower-level machinery |
| 3 | `NMO_API_TIER_PUBLIC_PROTOCOL` | Code that must match the Virtools file format or the plugin ABI |

## Stability promises

libnmo follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the version is below 1.0, any release may contain breaking changes, but
the tiers decide how those changes are handled.

### Tier 1: stable consumer

- Changes should add things, not break them: new functions, new struct members
  at the end, or new enum values.
- Renames, removals, and signature changes go through a deprecation period,
  get a `CHANGELOG.md` entry, and come with a migration note in `docs/`.
- From 1.0 onward, source-incompatible changes are made only in a major
  release.
- Callers allocate view structs and the library fills them in, so adding a
  member changes the ABI. Rebuild against the new headers when upgrading.
- Lua bindings (`nmo.*` modules) are part of this tier. The Lua table pushers
  in `src/lua/lua_pushers_generated.c` are generated from Tier 1 headers, so a
  view struct change is also a Lua-visible change.

### Tier 2: advanced C

- Public so that the CLI and other C tooling can drive sessions, indexes,
  pipelines, and registries directly.
- May change in any minor release. Breaking changes are listed in
  `CHANGELOG.md`, but no deprecation period is guaranteed.
- Not intended for bindings. Prefer a Tier 1 entry point where one exists.

### Tier 3: public protocol

- Exposes the on-disk format (chunk layouts, object layouts, InterfaceChunk
  sections), type-system and reflection authoring, and the extension plugin ABI.
- Changes follow format fidelity: if a real Virtools file needs different
  handling, this layer changes to match, even in a patch release.
- The extension ABI is versioned separately. The host loads only plugins built
  for exactly `NMO_EXTENSION_ABI_VERSION` (`include/extension/nmo_extension_abi.h`),
  and any ABI change bumps that number.

## How tiers are declared

Each header defines a header kind and one tier macro per API family:

```c
#define NMO_WORKSPACE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_WORKSPACE_LIFECYCLE_API_TIER NMO_API_TIER_STABLE_CONSUMER
#define NMO_WORKSPACE_EDIT_API_TIER NMO_API_TIER_STABLE_CONSUMER
```

Header kinds:

- `NMO_PUBLIC_HEADER_KIND_SINGLE_TIER`: every family in the header has the same
  tier.
- `NMO_PUBLIC_HEADER_KIND_MIXED_TIER`: the header holds families of different
  tiers. For example, `runtime/nmo_context.h` has Tier 1 lifecycle functions
  and Tier 2 registry access.
- `NMO_PUBLIC_HEADER_KIND_EXCLUDED`: public, but deliberately outside binding
  scope. No header uses this kind yet.

`tests/unit/test_public_api_smoke.c` checks these declarations, so a tier
change fails the test until someone updates it on purpose.

To list the current assignments:

```bash
grep -rn '_API_TIER NMO_API_TIER_' include
```

## Tier 1 headers

| Header | Families |
| --- | --- |
| `runtime/nmo_context.h` | context lifecycle |
| `runtime/nmo_workspace.h` | workspace lifecycle, workspace edit |
| `document/nmo_document_load.h` | load workflow |
| `document/nmo_document_save.h` | save workflow |
| `document/nmo_document_stats.h` | file statistics |
| `object/nmo_object_repository.h` | object identity |
| `type/nmo_type_query.h` | scalar type lookup |
| `type/nmo_type_view.h` | type metadata views |
| `format/nmo_interface_view.h` | interface view read |
| `behavior/nmo_behavior_query.h` | behavior and script queries |
| `behavior/nmo_behavior_view.h` | behavior view read |
| `behavior/nmo_behavior_edit.h` | behavior edit |
| `behavior/nmo_behavior_execute.h` | behavior execution |
| `behavior/nmo_script_edit.h` | script edit transactions |
| `lua/nmo_lua_runtime.h` | Lua runtime |
| `lua/nmo_lua_module.h` | Lua module registration |
| `lua/nmo_lua_bindings.h` | Lua bindings |
| `lua/nmo_lua_handles.h` | Lua handles |
| `lua/nmo_lua_behavior.h` | Lua runtime of a behavior execution |

Any header not listed here is Tier 2 or Tier 3. Check its `_API_TIER` macros.
