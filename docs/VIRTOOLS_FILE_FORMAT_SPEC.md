# Virtools File Format Specification (v8)

## Document Information

**Version**: 1.2
**Date**: 2025-11-09
**Based on**: CK2 Implementation Analysis (Verified)
**Current Format Version**: 8
**File Extension**: `.nmo`, `.cmo`, `.vmo` (various Virtools formats)

---

## Table of Contents

- [Overview](#1-overview)
- [File Structure](#2-file-structure)
- [CKFile Runtime & API Overview](#ckfile-runtime--api-overview)
- [CKObject System & Manager](#ckobject-system--manager)
- [Header Specification](#3-header-specification)
- [CKStateChunk Format](#4-ckstatechunk-format)
- [Data Serialization](#5-data-serialization)
- [Compression](#6-compression)
- [Checksum Validation](#7-checksum-validation)
- [Object References](#8-object-references)
- [Plugin Dependencies](#9-plugin-dependencies)
- [Manager Data](#10-manager-data)
- [Included Files](#11-included-files)
- [Load/Save Workflows](#12-loadsave-workflows)
- [Constants Reference](#13-constants-reference)
- [Implementation Notes](#14-implementation-notes)
- [Appendix](#15-appendix)
- [Revision History](#revision-history)

---

## 1. Overview

### 1.1 Purpose

The Virtools file format is a binary format designed for storing 3D content, behaviors, and multimedia data created with the Virtools SDK. It provides:

- **Efficient binary storage** with optional compression
- **Object graph serialization** with dependency tracking
- **Plugin dependency management** for extensibility
- **Backward compatibility** across multiple versions
- **Data integrity** through CRC validation

### 1.2 Design Philosophy

- **Chunk-based architecture**: Data organized into self-describing chunks
- **Manager extensibility**: Subsystems can store custom data
- **Reference preservation**: Complex inter-object relationships maintained
- **Version resilience**: Supports files from version 2 through 9

### 1.3 Magic Number

All Virtools files begin with the ASCII string `"Nemo Fi"` followed by the null terminator written by `strcpy`:
```
Bytes: 4E 65 6D 6F 20 46 69 00
ASCII: "Nemo Fi" + '\0'
```

There is no format-dependent variant byte; the 8th byte is always zero.

**Implementation**: `src/CKFile.cpp:944-970` (initialisation of `CKFileHeaderPart0::Signature`)

---

## 2. File Structure

### 2.1 Overall Layout

```
+---------------------------+----------------------------+
| File Header Part0         | 32 bytes, always present   |
+---------------------------+----------------------------+
| File Header Part1         | +32 bytes, only when       |
|                           | FileVersion >= 5 & size>=64|
+---------------------------+----------------------------+
| Header1 Section           | Variable. Contains object  |
|                           | descriptors (v7+) and the  |
|                           | plugin dependency table    |
|                           | (v8+). Eight extra bytes   |
|                           | are reserved for an        |
|                           | "included files" directory |
|                           | but the writer never fills |
|                           | that block, so the loader  |
|                           | always sees zero entries.  |
+---------------------------+----------------------------+
| Data Section              | Variable. Manager chunks   |
|                           | (v6+) precede object       |
|                           | chunks. Compression is     |
|                           | controlled by FileWriteMode|
+---------------------------+----------------------------+
| Included Files Payload    | Optional raw data appended |
|                           | after Data. This portion   |
|                           | is invisible to the stored |
|                           | checksum and file size.    |
+---------------------------+----------------------------+
```

### 2.2 Size Calculations

- **Header Part0/Part1**: Part0 is always 32 bytes. Part1 adds another 32 bytes but only exists for files with `FileVersion >= 5` and at least 64 bytes of data. Older files therefore expose only Part0.
- **Header1 Size**: Read from `Hdr1PackSize` (Part0) / `Hdr1UnPackSize` (Part1). When Part1 is missing both values are zero and the loader starts reading the data section immediately after Part0.
- **Data Size**: `DataPackSize` / `DataUnPackSize` describe the manager/object buffer only. They never include the optional included-file payload that `EndSave` appends afterwards.
- **Stored File Size**: The writer records `sizeof(CKFileHeader) + Hdr1PackSize + DataPackSize`. Appended included files sit after that range, so neither the checksum nor `m_FileInfo.FileSize` accounts for them.

**Implementation**: `src/CKFile.cpp:343-520`

## CKFile Runtime & API Overview

This section links the low-level format to the public APIs you must mirror when implementing a compatible loader/saver.

### Lifecycle and Entry Points

- `CreateCKFile`/`DeleteCKFile` allocate the `CKFile` wrapper used throughout the engine (`include/CKFile.h:176-276`). All state is reset via `ClearData` before reuse (`src/CKFile.cpp:299-341`).
- **Loading**:
  1. `OpenFile` maps the file through `VxMemoryMappedFile`, remembers the filename, and forwards to `OpenMemory` (`src/CKFile.cpp:203-235`).
  2. `OpenMemory` performs signature checks and kicks off `ReadFileHeaders`, storing metadata in `m_FileInfo` and optionally reporting missing plugins (`src/CKFile.cpp:220-520`).
  3. `Load`/`LoadFileData` call `ReadFileData` (if the data was not already staged), then invoke `FinishLoading` to instantiate objects, remap references, and dispatch `Load`/`PostLoad`/`ApplyPatchLoad` hooks (`src/CKFile.cpp:697-1587`).
  4. During the load, the context toggles `m_InLoad`, calls `ExecuteManagersPreLoad/PostLoad`, and resets automatic load/user callbacks when done.
- **Saving**:
  1. `StartSave` clears transient state, records the output name, and notifies behaviors with `CKM_BEHAVIORPRESAVE` while managers receive `ExecuteManagersPreSave` (`src/CKFile.cpp:706-780`).
  2. `SaveObject`, `SaveObjects`, and `SaveObjectAsReference` populate `m_FileObjects`, `m_ObjectsHashTable`, and `m_IndexByClassId` (see below) and update `m_SaveIDMax` (`src/CKFile.cpp:780-842`).
  3. `EndSave` finalizes all `CKFileObject` chunks, asks the UI callback to build interface chunks for scripts, serializes manager data, Header1, the data buffer, computes the Adler checksum, appends included files, and finally calls `ExecuteManagersPostSave` (`src/CKFile.cpp:840-1157`).
- `LoadAndSave` is a convenience upgrader that loads a file in the current runtime and re-saves it with the latest format rules (`src/CKFile.cpp:1186-1211`).
- All `CKContext::Load`/`Save` entry points ultimately forward to `g_ThePluginManager`, which instantiates a `CKFile`, performs the requested operation, and then destroys it (`src/CKContext.cpp:900-1016`).

### Metadata Access

- `CKFileInfo` mirrors the fields stored in Part0/Part1 and is filled either during `ReadFileHeaders` or by `CKContext::GetFileInfo` when you only need metadata (`include/CKTypes.h:138-161`, `src/CKContext.cpp:900-934`).
- `CKContext::GetFileInfo` reads the first 64 bytes from disk or memory, applies the same legacy guards as `ReadFileHeaders`, and avoids allocating a full `CKFile` instance when you simply want size/version data.

### Core Working Sets

- `CKFileObject` captures the per-object metadata, name, serialized `CKStateChunk`, and remap options used during both load and save (`include/CKFile.h:42-118`). Each entry tracks pre/post compression sizes, file offset, and creation options (`CK_FO_OPTIONS`).
- `CKFileManagerData` couples a manager GUID with its saved chunk (`include/CKFile.h:12-22`). Managers are serialized before objects and reloaded ahead of the object graph.
- `CKFilePluginDependencies` holds the dependency category, GUID list, and an `XBitArray` tracking which GUIDs are present in the current runtime (`include/CKFile.h:25-39`).
- `CKBufferParser` abstracts reading from either an in-memory buffer or a mapped file. It owns the compression helpers (`Pack`, `UnPack`, `ComputeCRC`) and file-extraction routines used when handling Header1, the data section, and appended files (`include/CKFile.h:120-173`, `src/CKFile.cpp:60-188`).

### Context & Manager Coordination

- The `CKContext` injects its lifecycle around file I/O: `ExecuteManagersPreLoad/PostLoad`, `ExecuteManagersPreSave/PostSave`, `WarnAllBehaviors`, `SetAutomaticLoadMode`, and `SetUserLoadCallback` are all driven inside `LoadFileData` / `StartSave` / `EndSave` (`src/CKFile.cpp:697-1157`).
- `CKObjectManager::StartLoadSession/EndLoadSession` ensures new IDs do not collide with existing runtime objects. Objects are registered via `RegisterLoadObject` the moment they are created (`src/CKFile.cpp:1238-1587`).
- UI callbacks are invoked twice: once to request interface chunks for scripted behaviors before writing, and once per object during load/save progress updates via `CKUIM_LOADSAVEPROGRESS` (`src/CKFile.cpp:843-932`, `src/CKFile.cpp:1380-1490`).
- Manager data is saved with `manager->SaveData` and restored prior to object instantiation with `manager->LoadData`. After object chunks are loaded, each `CKBeObject` receives `ApplyOwner`/`ApplyPatchForOlderVersion`, and behaviors call `ApplyPatchLoad` plus `CKM_BEHAVIORLOAD` callbacks (`src/CKFile.cpp:900-1587`).
- Plugin dependencies are recomputed on save (`CKPluginManager::ComputeDependenciesList`) and validated on load using `FindComponent`. Missing plugins keep their GUID in `m_PluginsDep` so callers can inspect the failure (`src/CKFile.cpp:438-520`, `src/CKFile.cpp:449-520`).

### Load Flags and Automatic Mode

- `CK_LOAD_FLAGS` control how `CKFile` adjusts its behavior (`include/CKEnums.h:1166-1176`):
  - `CK_LOAD_CHECKDEPENDENCIES` enables GUID validation in Header1 and, for legacy files, forces the loader to read the full data section so it can rebuild dependency information (`src/CKFile.cpp:449-520`).
  - `CK_LOAD_ONLYBEHAVIORS` skips non-behavior chunks, bypasses object-remap logic, and suppresses manager remapping (`src/CKFile.cpp:604-1507`).
  - `CK_LOAD_DODIALOG`, `CK_LOAD_AUTOMATICMODE`, and `CK_LOAD_CHECKDUPLICATES` make `FinishLoading` ask `CKObjectManager` to resolve duplicate names through `CK_OBJECTCREATION_ASK` rather than silently creating objects (`src/CKFile.cpp:1254-1263`).
  - `CK_LOAD_AS_DYNAMIC_OBJECT` propagates `CK_OBJECTCREATION_DYNAMIC`, allowing the resulting objects to be destroyed later without touching the original project (`src/CKFile.cpp:1257-1263`).
  - Other flags (`CK_LOAD_GEOMETRY`, `CK_LOAD_ANIMATION`, `CK_LOAD_ASCHARACTER`) are honored by higher-level helpers such as `CKContext::Load` and the plugin manager; the serializer itself only inspects the flags listed above.
- Automatic load mode: when duplicates are detected and `CK_LOAD_AUTOMATICMODE` is set, `CKContext` must be configured via `SetAutomaticLoadMode` before calling into the loader. `CKFile` resets this mode back to `CKLOAD_INVALID` after every load attempt (`src/CKFile.cpp:538-695, 697-758`).

### Error Handling and Warning Flow

- `ReadFileHeaders` and `ReadFileData` return descriptive `CKERROR` codes (`src/CKFile.cpp:343-689`):
  - `CKERR_INVALIDFILE` for short buffers, signature mismatches, unpack failures, or truncated sections.
  - `CKERR_OBSOLETEVIRTOOLS` when `FileVersion >= 10`.
  - `CKERR_FILECRCERROR` when the Adler checksum does not match.
  - `CKERR_PLUGINSMISSING` when dependency validation fails (the load can proceed, but the caller is informed).
  - `CKERR_CANTWRITETOFILE` / `CKERR_NOTENOUGHDISKPLACE` during save if the target cannot be opened or flushed.
- `WarningForOlderVersion` is raised whenever Part0 reports legacy constructs (non-zero `FileVersion2`, pre-v4 object tables, etc.). After loading such files the context prints "Obsolete File Format, Please Re-Save..." to encourage a modern rewrite (`src/CKFile.cpp:356-520, 697-758`).

### Reference Handling and Renaming

- `CKFileObject::Options` drives what happens when an incoming chunk collides with an existing object: `CK_FO_DEFAULT`, `CK_FO_RENAMEOBJECT`, `CK_FO_REPLACEOBJECT`, or `CK_FO_DONTLOADOBJECT` (`include/CKFile.h:52-96`).
- `SaveObjectAsReference` and the `m_ReferencedObjects` list mark dependencies that should keep their original IDs but should not be serialized again. `EndSave` temporarily sets `CK_OBJECT_ONLYFORFILEREFERENCE` so Header1 can flag those entries with bit `0x800000` (`src/CKFile.cpp:810-915`, `src/CKFile.cpp:984-1042`).
- Negative IDs inside Header1 trigger `ResolveReference`, which currently supports parameter objects by matching on name and parameter type (`src/CKFile.cpp:1504-1587`).
- `RemapManagerInt` allows compatibility patches to translate manager-specific integer IDs after loading, before managers apply their data (`src/CKFile.cpp:1186-1220`).

### Extensibility Hooks and Upgrade Paths

- `IncludeFile` relies on `CKPathManager::ResolveFileName` to queue external assets for embedding; Section 11 covers the current limitation where extraction is not implemented on load (`src/CKFile.cpp:1127-1179`).
- `GetMissingPlugins` exposes the dependency table so tools can surface actionable warnings to users (`include/CKFile.h:217-224`).
- `LoadAndSave` plus `m_Context->WarnAllBehaviors` and the UI callback hooks make it possible to batch-upgrade legacy files with minimal custom code.

These behaviors are part of the observable contract; a compatible loader/saver must reproduce them for Virtools projects to round-trip correctly.

---

## CKObject System & Manager

Understanding the CK object model is crucial because `CKFile` is merely the persistence layer over this hierarchy.

### Hierarchy Snapshot

- **CKObject** (`include/CKObject.h:1-260`) is the abstract base for everything serialized to disk. Each instance owns:
  - A globally unique `CK_ID` assigned by `CKContext::CreateObject` / `CKObjectManager::RegisterObject`.
  - A `CK_CLASSID` used for filtering and load ordering. The class tree is stored in `g_CKClassInfo` (`src/CKFile.cpp:44`) and queried through `XBitArray inclusion/exclusion` masks during load.
  - Optional name/app data strings plus `CK_OBJECT_FLAGS` such as `CK_OBJECT_ONLYFORFILEREFERENCE`, `CK_OBJECT_NOTTOBESAVED`, or `CK_OBJECT_VISIBLE`.
  - Virtual hooks used by the serializer: `PreSave`, `Save`, `Load`, and `PostLoad`. `CKFile::SaveObject` calls `PreSave` followed by `Save` to obtain a `CKStateChunk`, while `FinishLoading` calls `Load` and `PostLoad` in dependency order (`src/CKFile.cpp:780-1587`).
- Derived families matter for load ordering:
  - `CKBeObject` (gameplay objects) -> `CK3dEntity`/`CK2dEntity` -> renderable classes.
  - `CKParameter`, `CKParameterIn`, `CKParameterOut`, `CKParameterLocal` share parameter-type metadata and can serve as file references resolved by name/type (see `ResolveReference`, `src/CKFile.cpp:1504-1587`).
  - `CKBehavior` adds script-specific state, interface chunks, and patch hooks invoked after load (`src/CKFile.cpp:1439-1490`).

### CKObjectManager Responsibilities

- Declared in `include/CKObjectManager.h`, the manager keeps the authoritative list of objects (`m_Objects`), per-class arrays (`m_ClassLists`), and the mapping between IDs and pointers.
- Key services used by the file layer:
  - `RegisterObject` / `FinishRegisterObject` assign IDs for new runtime objects; `UnRegisterObject` and `DeleteObjects` tear them down safely.
  - `StartLoadSession(maxId)` reserves a block of IDs (typically `m_SaveIDMax + 1`) before loading, `RegisterLoadObject` records the mapping between a file ID and the newly created runtime object, and `EndLoadSession` releases temporary state. `CKFile::LoadFindObject` reads from `m_FileObjects[index].CreatedObject`, which was filled via this registration.
  - `GetObjectsCountByClassID` / `GetObjectsListByClassID` allow routines such as `ResolveReference` to look up existing parameters by name/type.
  - Dynamic object helpers (`SetDynamic`, `UnSetDynamic`, `DeleteAllDynamicObjects`, `m_DynamicObjects`) honor `CK_LOAD_AS_DYNAMIC_OBJECT` so temporary loads can be destroyed later without touching the original project.
  - App data tables (`m_ObjectAppData`) and deferred deletions are updated when `CKFile` duplicates or replaces objects during load.

### Interaction with CKFile

- `m_AlreadySavedMask`, `m_AlreadyReferencedMask`, and `m_ObjectsHashTable` inside `CKFile` mirror the manager’s view so object IDs in chunks can be replaced by file indices when `CHNK_OPTION_FILE` is set (`include/CKFile.h:217-275`, `src/CKStateChunk.cpp:552-644`).
- `g_CKClassInfo` plus `m_IndexByClassId` drive the deterministic load order: beobjects first (excluding behaviors), local/input/output parameters next, and behaviors last, matching dependencies the runtime expects (`src/CKFile.cpp:1238-1490`).
- When `SaveObjectAsReference` queues an ID (e.g., shared parameter or mesh), `CKFile` marks the runtime object with `CK_OBJECT_ONLYFORFILEREFERENCE`. Header1 encodes this via bit `0x800000`, and `FinishLoading` uses the flag to skip chunk loading while still remapping IDs (`src/CKFile.cpp:810-1042`).
- `CKObjectManager::SetDynamic` is invoked whenever `CK_LOAD_AS_DYNAMIC_OBJECT` is set so objects created during load can later be culled via `DeleteAllDynamicObjects`. The flag also influences `CKObject::IsDynamic` checks when deciding whether to save or reference an object (`src/CKFile.cpp:780-915`).

### ID Remapping Pipeline

1. `StartLoadSession(m_SaveIDMax + 1)` instructs the manager to keep file IDs stable by ensuring no runtime object receives an ID inside the saved range (`src/CKFile.cpp:1238-1263`).
2. As each file object is instantiated, `RegisterLoadObject` stores the new ID so `LoadFindObject(fileIndex)` can resolve references when chunks call `ReadObjectID`.
3. After all chunks are staged, `CKStateChunk::RemapObjects(m_Context)` walks every chunk and replaces file indices with real runtime IDs, including nested sub-chunks (Section 4.11). Managers receive the same treatment via `CKStateChunk::RemapManagerInt`.
4. If `CK_LOAD_ONLYBEHAVIORS` is *not* set, parameter and manager remappers run before any `Load` calls so by the time `CKObject::Load` executes, every embedded ID already targets a live runtime object.

Together, these contracts define how `CKFile` cooperates with the broader object system; any compatible implementation must honor them to maintain referential integrity.

### CK Object Flags & Lifecycles

Relevant `CK_OBJECT_FLAGS` (see `include/CKDefines.h:30-80`) that directly influence file I/O:

| Flag | Purpose during Save/Load |
|------|-------------------------|
| `CK_OBJECT_NOTTOBESAVED` | Causes `SaveObject` / `SaveObjectAsReference` to skip the object entirely (used for editor-only helpers). |
| `CK_OBJECT_DYNAMIC` | Set when created inside `CK_LOAD_AS_DYNAMIC_OBJECT` sessions; `SaveObject` refuses to write dynamic instances and `StartLoadSession` records them separately so `DeleteAllDynamicObjects` can destroy them later. |
| `CK_OBJECT_ONLYFORFILEREFERENCE` | Temporarily set by `EndSave` for `m_ReferencedObjects`. Header1 writes their IDs with bit `0x800000`, and `FinishLoading` interprets such entries as references instead of chunks. |
| `CK_OBJECT_VISIBLE` / `CK_OBJECT_HIERACHICALHIDE` | Persisted through `CKStateChunk` data, primarily for scene graph reconstruction. |

Object lifecycle hooks must run in a strict order:

```text
Save path:
  PreSave(file, flags)  -> object ensures dependencies exist
  Save(file, flags)     -> returns CKStateChunk with identifiers + payload
  (CKFile writes manager/behavior patches)

Load path:
  Load(chunk, file)     -> object reads identifiers, reconstructs state
  PostLoad()            -> final fixups once every dependency exists
  ApplyPatchLoad/ApplyOwner (class-specific) -> e.g., CKBehavior, CKBeObject
```

When implementing your own loader you must invoke these hooks (or their equivalents) to reproduce internal side effects such as re-registering animations, refreshing behavior inputs, or restoring visibility state.

### CKObjectManager Data Structures

`CKObjectManager` maintains several arrays/bitsets (`include/CKObjectManager.h:1-146`, `src/CKObjectManager.cpp`) that any compatible runtime must mimic:

- `m_Objects`: dense array indexed by raw `CK_ID`. Deleted IDs are recycled through `m_FreeObjectIDs`.
- `m_ClassLists`: per-class `XObjectArray` used to build `g_CKClassInfo[classId].Children` for fast filtering.
- `m_LoadSession`: temporary array storing the mapping from file indices to newly created runtime IDs. The loader must allocate at least `(m_SaveIDMax + 1)` entries before reading chunks so that original IDs remain valid; `RegisterLoadObject` writes tuples `(fileId -> createdId)`.
- `m_DynamicObjects`: list of IDs flagged as dynamic. `DeleteAllDynamicObjects` iterates this list after dynamic loads and calls `DestroyObject` on each entry.
- `m_SceneGlobalIndex` / `m_GroupGlobalIndex`: bitsets used for allocation of scene/group handles; `StartLoadSession` cleans them if no level existed beforehand.

Pseudo-code for the manager interaction during load:

```text
function beginLoad(maxSavedId):
    objectManager.StartLoadSession(maxSavedId + 1)

function createObject(descriptor):
    obj, mode = context.CreateObject(descriptor.cid, descriptor.name, creationOptions(flags))
    fileObj.ObjPtr = obj
    fileObj.CreatedObject = obj.GetID()
    objectManager.RegisterLoadObject(obj, descriptor.Object)

function endLoad():
    objectManager.EndLoadSession()
```

Any alternative implementation must provide equivalent APIs so that ID remapping, duplicate detection, and dynamic object cleanup mirror the behavior of the original Virtools runtime.

---

## 3. Header Specification

### 3.1 File Header Part0 (32 bytes)

| Offset | Size | Type   | Field          | Description                              |
|--------|------|--------|----------------|------------------------------------------|
| 0x00   | 8    | BYTE[] | Signature      | Magic number "Nemo Fi" + variant         |
| 0x08   | 4    | DWORD  | Crc            | Adler-32 checksum (see Section 7)        |
| 0x0C   | 4    | DWORD  | CKVersion      | Virtools engine version                  |
| 0x10   | 4    | DWORD  | FileVersion    | File format version (current: 8)         |
| 0x14   | 4    | DWORD  | FileVersion2   | Legacy field; non-zero triggers legacy path |
| 0x18   | 4    | DWORD  | FileWriteMode  | Compression/save options flags           |
| 0x1C   | 4    | DWORD  | Hdr1PackSize   | Compressed size of Header1 section       |

**Structure Definition**: `CKFile.cpp:14-22`

#### Field Details

**Signature (8 bytes)**
- First 7 bytes: Always "Nemo Fi" (0x4E656D6F204669)
- Last byte: Varies by file type/version
- Used for file type identification

**Crc (4 bytes)**
- Adler-32 checksum produced by `CKComputeDataCRC` (zlib's `adler32`)
- For `FileVersion >= 8` it covers: Part0/Part1 (with Crc zeroed) + Header1 + Data
- For older files it is recomputed against the uncompressed data section only
- Included-file payloads appended after the data buffer are *never* part of this checksum

**CKVersion (4 bytes)**
- Virtools engine version that created the file
- Format: 0xVVSSBBBB (V=major, S=minor, B=build)
- Not used for format validation, informational only

**FileVersion (4 bytes)**
- File format version: valid range [2, 9]
- Current version: 8
- Files with version < 2: Obsolete warning
- Files with version >= 10: Rejected as too new
- Files saved by this repository always write version 8

**FileVersion2 (4 bytes)**
- Historical compatibility slot. When it is non-zero the loader zeroes the entire Part0 structure, sets `WarningForOlderVersion`, and continues using the legacy code paths (`src/CKFile.cpp:356-365`).

**FileWriteMode (4 bytes)**
- Bit flags controlling compression and save options
- See [Section 13.2](#132-filewritemode-flags) for values

**Hdr1PackSize (4 bytes)**
- Size in bytes of the Header1 section (as stored in file)
- If compressed: size after compression
- If uncompressed: equals Hdr1UnPackSize

### 3.2 File Header Part1 (32 bytes)

| Offset | Size | Type   | Field          | Description                              |
|--------|------|--------|----------------|------------------------------------------|
| 0x20   | 4    | DWORD  | DataPackSize   | Compressed size of Data section          |
| 0x24   | 4    | DWORD  | DataUnPackSize | Uncompressed size of Data section        |
| 0x28   | 4    | DWORD  | ManagerCount   | Number of managers with saved data       |
| 0x2C   | 4    | DWORD  | ObjectCount    | Number of objects in file                |
| 0x30   | 4    | DWORD  | MaxIDSaved     | Highest object ID used                   |
| 0x34   | 4    | DWORD  | ProductVersion | Virtools product version                 |
| 0x38   | 4    | DWORD  | ProductBuild   | Virtools product build number            |
| 0x3C   | 4    | DWORD  | Hdr1UnPackSize | Uncompressed size of Header1 section     |

**Structure Definition**: `CKFile.cpp:24-32`

#### Field Details

**DataPackSize / DataUnPackSize (4 bytes each)**
- PackSize: Compressed size (or uncompressed if no compression)
- UnPackSize: Original uncompressed size
- Compression ratio = PackSize / UnPackSize

**ManagerCount (4 bytes)**
- Number of managers that saved data
- Manager data appears before object data in Data section
- Range: 0 to total number of registered managers

**ObjectCount (4 bytes)**
- Total number of objects stored in file
- Includes all object types (behaviors, entities, parameters, etc.)
- Used to allocate m_FileObjects array during load

**MaxIDSaved (4 bytes)**
- Maximum CK_ID value used in file
- Used to prevent ID collisions during load
- Context adjusts next available ID to be > MaxIDSaved

**ProductVersion / ProductBuild (4 bytes each)**
- Virtools Dev/Player version information
- Format similar to CKVersion
- Informational only, not used for validation
- Values >= 12 are normalised to `(0, 0x1010000)` during load to match the historical PC versions (`src/CKFile.cpp:369-377`)

### 3.3 Version History

| Version | Introduced Features                                      |
|---------|----------------------------------------------------------|
| 2       | Basic format (earliest supported)                        |
| 3-4     | Legacy formats with different header layouts             |
| 5       | Added second header part (Part1)                         |
| 6       | Added manager data storage capability                    |
| 7       | Object list moved to Header1, added file indices         |
| 8       | Adler-32 over header/data, plugin dependency block; included-file directory reserved but not written |
| 9       | Reserved for future use                                  |
| 10+     | Rejected as too new for current implementation           |

**Implementation**: `CKFile.cpp:360-382` (version checks)

---

## 4. CKStateChunk Format

### 4.1 Overview

CKStateChunk is the fundamental serialization unit in Virtools files. It's a binary container that stores:
- Raw data buffer (DWORD-aligned)
- Object ID references
- Sub-chunks (nested chunks)
- Manager references
- Version information

### 4.2 Binary Format

```
┌─────────────────────────────────────────┐
│  Version Info             (4 bytes)     │  Packed: DataVersion | ClassID | ChunkVersion | ChunkOptions
├─────────────────────────────────────────┤
│  Chunk Size              (4 bytes)     │  Size in DWORDs (not bytes)
├─────────────────────────────────────────┤
│  Data Buffer             (variable)     │  Size = ChunkSize * 4 bytes
├─────────────────────────────────────────┤
│  [Optional] IDs List     (variable)     │  If CHNK_OPTION_IDS flag set
│    - ID Count            (4 bytes)      │
│    - ID Array            (Count * 4)    │
├─────────────────────────────────────────┤
│  [Optional] Chunks List  (variable)     │  If CHNK_OPTION_CHN flag set
│    - Chunk Count         (4 bytes)      │
│    - Chunk Array         (recursive)    │
├─────────────────────────────────────────┤
│  [Optional] Managers List (variable)    │  If CHNK_OPTION_MAN flag set
│    - Manager Count       (4 bytes)      │
│    - Manager Array       (Count * 4)    │
└─────────────────────────────────────────┘
```

**Implementation**: `CKStateChunk.cpp:1310-1520` (ConvertToBuffer/ConvertFromBuffer)

### 4.3 Version Info (4 bytes)

Packed as a single DWORD with **four** components (not three):

```
Bits [0-7]:   DataVersion    (8 bits)  - Custom version per object class
Bits [8-15]:  ChunkClassID   (8 bits)  - Class identifier (used in legacy code)
Bits [16-23]: ChunkVersion   (8 bits)  - Chunk format version (current: 7)
Bits [24-31]: ChunkOptions   (8 bits)  - Chunk option flags (IDS, MAN, CHN, etc.)
```

**Packing Formula** (actual implementation):
```c
// Step 1: Pack chunk version with options in high byte
CKWORD chunkVersion = m_ChunkVersion | (m_ChunkOptions << 8);

// Step 2: Pack data version with class ID in high byte
CKWORD dataVersion = m_DataVersion | (m_ChunkClassID << 8);

// Step 3: Combine into single DWORD
DWORD versionInfo = dataVersion | (chunkVersion << 16);
```

**Unpacking Formula**:
```c
// Extract from DWORD
CKWORD dataVersion = versionInfo & 0xFFFF;
CKWORD chunkVersion = (versionInfo >> 16) & 0xFFFF;

// Extract individual components and mask to remove upper bits
m_DataVersion = dataVersion & 0x00FF;        // Mask to keep only version
m_ChunkClassID = (dataVersion >> 8) & 0xFF;  // Extract class ID
m_ChunkVersion = chunkVersion & 0x00FF;      // Mask to keep only version
m_ChunkOptions = (chunkVersion >> 8) & 0xFF; // Extract options
```

**Current Chunk Version**: `CHUNK_VERSION4 = 7`

**IMPORTANT**: The chunk options are embedded in the version info during serialization. After unpacking, the version fields must be masked to `0x00FF` to remove the option bits.

**Implementation**: `CKStateChunk.cpp:1332-1334` (write), `CKStateChunk.cpp:1463-1467` (read)

### 4.4 Chunk Size (4 bytes)

- **Unit**: DWORDs (4-byte units), NOT bytes
- **Byte Size**: ChunkSize * 4
- **Includes**: Only the main data buffer, NOT optional lists
- **Alignment**: All data in chunk is DWORD-aligned

### 4.5 Data Buffer

- **Size**: ChunkSize * 4 bytes
- **Alignment**: DWORD (4-byte) aligned
- **Access**: Sequential read/write via m_CurrentPos pointer
- **Format**: Mixed-type binary data (see [Section 5](#5-data-serialization))

### 4.6 Optional Lists

#### IDs List (CHNK_OPTION_IDS = 0x01)

Stores references to other objects:

```
[4 bytes] ID Count (n)
[4 * n]   Object IDs or File Indices
```

- **Save Mode**: Object IDs converted to file indices (0-based)
- **Load Mode**: File indices converted back to object IDs
- **Special Values**:
  - `-1`: NULL reference
- `ID | 0x800000`: Bit 23 marks entries flagged `CK_OBJECT_ONLYFORFILEREFERENCE` (referenced but not serialized)

**Implementation**: `CKStateChunk.cpp:613-672` (WriteObjectID/ReadObjectID)

#### Chunks List (CHNK_OPTION_CHN = 0x04)

Stores nested sub-chunks:

```
[4 bytes] Chunk Count (n)
[Variable] n CKStateChunks (recursive format)
```

Each sub-chunk follows the complete CKStateChunk binary format recursively.

**Implementation**: `CKStateChunk.cpp:739-824` (AddChunk/ReadChunk)

#### Managers List (CHNK_OPTION_MAN = 0x02)

Stores integer references to managers:

```
[4 bytes] Manager Count (n)
[4 * n]   Manager Indices
```

Manager indices are 0-based indices into the context's manager list.

**Implementation**: `CKStateChunk.cpp:682-737` (WriteManagerInt/ReadManagerInt)

### 4.7 Chunk Options

Chunks store their options in the `m_ChunkOptions` field. These options are embedded in the version info during serialization (see [Section 4.3](#43-version-info-4-bytes)):

| Flag                    | Value | Description                                    |
|-------------------------|-------|------------------------------------------------|
| CHNK_OPTION_IDS         | 0x01  | Contains object ID references                  |
| CHNK_OPTION_MAN         | 0x02  | Contains manager integer refs                  |
| CHNK_OPTION_CHN         | 0x04  | Contains sub-chunks                            |
| CHNK_OPTION_FILE        | 0x08  | Written with file context (indices vs IDs)     |
| CHNK_OPTION_ALLOWDYN    | 0x10  | Dynamic object can be written in chunk         |
| CHNK_OPTION_LISTBIG     | 0x20  | Lists stored in big endian                     |
| CHNK_DONTDELETE_PTR     | 0x40  | Data buffer not owned by chunk                 |
| CHNK_DONTDELETE_PARSER  | 0x80  | Parser not owned by chunk                      |

**Note**: Options with values 0x01-0x10 are serialized in the version info. Options 0x20-0x80 are runtime flags not saved to file.

**Implementation**: `CKStateChunk.h:140-150`

### 4.8 Identifier System

Chunks support tagged sections for non-linear reading:

```
[4 bytes] Identifier Tag (DWORD)
[4 bytes] Offset to Next Identifier (or 0 if last)
[Variable] Data until next identifier
```

**Operations**:
- `WriteIdentifier(tag)`: Creates a new tagged section
- `SeekIdentifier(tag)`: Jumps to tagged section
- `ReadIdentifier()`: Reads current section's tag

**Link Structure**: Forms a singly-linked list through the chunk data buffer:
```
Pos 0: [ID1][Next=100][...data...] -> Pos 100: [ID2][Next=200][...data...] -> Pos 200: [ID3][Next=0][...data...]
```

**Implementation**: `CKStateChunk.cpp:213-307`

### 4.9 Chunk Parser & Memory Growth

- `StartWrite` clears previous data, resets `ChunkParser::CurrentPos`, and sets `m_ChunkVersion = CHUNK_VERSION4` for every freshly written chunk (`src/CKStateChunk.cpp:132-160`). The parser keeps track of `DataSize` and the last identifier position (`PrevIdentifierPos`) so nested tags can be rewound safely.
- `CheckSize` guarantees capacity in DWORD units. It grows the backing buffer by at least 500 DWORDs at a time, copying existing contents and updating `DataSize`. Because the entire chunk is DWORD-aligned, every write (`WriteInt`, `WriteWord`, etc.) simply increments `CurrentPos` by the number of DWORDs consumed (`src/CKStateChunk.cpp:146-214`, `src/CKStateChunk.cpp:948-1108`).
- `ConvertToBuffer` serializes the chunk into the wire format described earlier. It inspects the optional lists to set `CHNK_OPTION_IDS/MAN/CHN/FILE` bits, then writes `[versionInfo][chunkSize][data][ids][chunks][managers]` contiguously. `ConvertFromBuffer` reverses the process, auto-detecting legacy formats (`CHUNK_VERSION < 5`) and normalizing the version/options bytes (`src/CKStateChunk.cpp:1310-1522`).
- `CHNK_OPTION_ALLOWDYN` is set when `m_File` is non-null *and* the owning object is dynamic; this lets runtime-only objects serialize themselves into chunks without tripping the static-object checks in `WriteObject`/`WriteObjectID` (`src/CKStateChunk.cpp:552-612`).

### 4.10 Sequence & List Helpers

- `IntListStruct` tracks offsets of IDs, sub-chunks, or manager entries by recording absolute positions inside `m_Data`. `AddEntry` pushes a single offset, while `AddEntries` inserts a `-1` sentinel followed by a position—this pattern is used for counted sequences (`include/CKStateChunk.h:36-118`).
- `StartObjectIDSequence`, `StartSubChunkSequence`, and `StartManagerSequence` reserve space for a count, push the current cursor into the matching list, and allow the caller to append repeated values without writing redundant metadata (`src/CKStateChunk.cpp:596-746`). The matching `StartReadSequence` / `StartManagerReadSequence` helpers retrieve the counts on the read side.
- `WriteObjectID`/`ReadObjectID` automatically bridge between runtime IDs and file indices: when `m_File` is set and `CHNK_OPTION_FILE` is active, the writer stores a file index, otherwise it writes the raw `CK_ID`. On load, positive indices are resolved through `CKFile::LoadFindObject`, negative values are treated as legacy references that trigger extra reads, and `-1` encodes `NULL` (`src/CKStateChunk.cpp:552-676`).
- `WriteSubChunkSequence` embeds child chunks as `[sizeInBytes][classId][version][chunkSize][flags]...` and records their offsets in `m_Chunks`, enabling recursive traversal without fully parsing the payload (`src/CKStateChunk.cpp:739-918`).

### 4.11 Remapping & Iteration Support

- `ChunkIteratorData` carries pointers to the chunk’s ID list, sub-chunk list, manager list, and optional conversion tables. `CKStateChunk::IterateAndDo` walks every entry (and nested sub-chunks) invoking a callback such as `ObjectRemapper`, `ManagerRemapper`, or `ParameterRemapper` (`include/CKStateChunk.h:8-74`, `src/CKStateChunk.cpp:1524-1692`).
- `RemapObjects(CKContext*)` uses `ChunkIteratorData` plus `CKDependenciesContext` (when provided) to convert every stored file index into the runtime `CK_ID`. This is called automatically by `CKFile::FinishLoading` once all objects exist. `RemapObject(oldId,newId)` and `RemapParameterInt` expose the same mechanism to callers that patch individual IDs (`src/CKStateChunk.cpp:1524-1705`).
- Manager-specific integers—such as texture slots or parameter enumerations—are patched using `RemapManagerInt(CKGUID,int*,int)`; each entry in the manager list is compared against the provided conversion table (`src/CKStateChunk.cpp:1707-1756`).
- Because `ConvertFromBuffer` stores the `chunkOptions` byte, the remap routines can detect whether a chunk was written relative to a file (`CHNK_OPTION_FILE`) before attempting to translate IDs. Chunks created entirely in-memory (e.g., via gameplay logic) skip the remap passes.

### 4.12 Bitmap & Raw Image Support

- CKStateChunk embeds images using the bitmap helpers near the end of `src/CKStateChunk.cpp`:
  - `WriteReaderBitmap` / `ReadReaderBitmap` serialize a `CKBitmapReader` GUID, optional `CKBitmapProperties`, and the compressed bitstream produced by the reader plug-in. The runtime consults `CKGetPluginManager()->GetBitmapReader` using the stored extension to recreate the texture or sprite.
  - `WriteBitmap` / `ReadBitmap` accept a `BITMAP_HANDLE`, normalize it into a `VxImageDescEx`, and store raw pixels plus format metadata. On load, the code reconstructs the bitmap (creating a new handle if needed) and optionally patches the description into the caller's buffer (`src/CKStateChunk.cpp:2160-2315`).
  - `WriteRawBitmap` / `ReadRawBitmap` are the lowest-level helpers. They write the width, height, pitch, format, and the tightly packed byte array so callers can sidestep plug-ins entirely when transporting uncompressed image data (`src/CKStateChunk.cpp:1908-2156`).
- All bitmap routines share the same layout: `[magic/fourcc][extension length + string][property flags][pixel payload]`. The chunk keeps everything DWORD-aligned so standard read/write helpers can walk the blob without additional padding.

### 4.13 CKBufferParser Utility Methods

`CKBufferParser` (declared in `include/CKFile.h:120-173`, implemented in `src/CKFile.cpp:60-188`) backs every chunk serialization. Key operations to replicate:

- `Write(void* data, int size)` / `Read(void* data, int size)` move raw bytes while advancing `m_CursorPos`. The parser never bounds-checks, so upstream calls to `CheckSize` must guarantee capacity.
- `ReadString()` reads a length-prefixed UTF-8 string, allocates `len + 1` bytes, and appends a null terminator. `ReadInt()` is the primitive used for nearly every scalar.
- `Seek(int pos)`/`Skip(int offset)` adjust the cursor relative to the beginning; these are heavily used when recomputing checksums (`ReadFileHeaders`) and when skipping packed sections.
- `ExtractChunk(size, CKFile*)` copies `size` bytes into a new buffer and wraps them in another parser so nested chunks can be processed without exposing the parent buffer.
- `ComputeCRC(size, previous)` applies `CKComputeDataCRC` (Adler-32) to the next `size` bytes and advances the cursor. `Pack` and `UnPack` call `CKPackData` / `CKUnPackData` to compress or inflate sections depending on `FileWriteMode`.
- `ExtractFile(filename, size)` writes the next `size` bytes to disk—this is how `CKFile` appends included files after the packed data buffer.

When implementing a compatible library, ensure your buffer wrapper exposes equivalent semantics; the higher-level algorithms assume cursor-based random access with lightweight slice extraction and optional compression.

### 4.14 ConvertToBuffer / ConvertFromBuffer Algorithms

Pseudocode mirroring `src/CKStateChunk.cpp:1310-1522`:

```text
function ConvertToBuffer(buffer):
    chunkOptions = 0
    size = sizeof(DWORD) * 2 + m_ChunkSize * 4
    if m_Ids:      size += m_Ids->Size * 4; chunkOptions |= CHNK_OPTION_IDS
    if m_Chunks:   size += m_Chunks->Size * 4; chunkOptions |= CHNK_OPTION_CHN
    if m_Managers: size += m_Managers->Size * 4; chunkOptions |= CHNK_OPTION_MAN
    if m_File:     chunkOptions |= CHNK_OPTION_FILE

    if buffer is null: return size

    buf[0] = (m_DataVersion | (m_ChunkClassID << 8)) | ( (m_ChunkVersion | (chunkOptions << 8)) << 16 )
    buf[1] = m_ChunkSize
    copy m_Data -> buf
    copy optional lists -> buf in order IDS -> CHN -> MAN
```

```text
function ConvertFromBuffer(buffer):
    val = buf[pos++]
    dataVersion = val & 0x0000FFFF
    chunkVersion = (val >> 16) & 0x0000FFFF

    if chunkVersion < CHUNK_VERSION2:
        read legacy layout:
            m_ChunkClassID = buf[pos++]
            m_ChunkSize = buf[pos++]
            skip reserved DWORD
            read idCount/chunkCount and copy arrays
    else if chunkVersion == CHUNK_VERSION2:
        same as above but also read managerCount
    else:
        chunkOptions = (chunkVersion & 0xFF00) >> 8
        m_ChunkClassID = (dataVersion & 0xFF00) >> 8
        m_ChunkSize    = buf[pos++]
        read data payload
        if !(chunkOptions & CHNK_OPTION_FILE): m_File = nullptr
        if chunkOptions & CHNK_OPTION_IDS: allocate IntListStruct, copy idCount ints
        repeat for CHN and MAN lists
    end

    m_DataVersion = dataVersion & 0x00FF
    m_ChunkVersion = chunkVersion & 0x00FF
```

These steps guarantee that even legacy chunks (pre-v5) can still be inflated into the modern in-memory representation before remapping occurs.

---

## 5. Data Serialization

### 5.1 Fundamental Principles

1. **DWORD Alignment**: All data padded to 4-byte boundaries
2. **Little Endian**: All multi-byte values stored little-endian
3. **Sequential Access**: Data written/read sequentially via cursor (m_CurrentPos)
4. **Type Prefixed**: Strings and buffers prefixed with size

### 5.2 Primitive Types

#### Byte (1 byte -> 4 bytes stored)

```
Write: [byte value] + [3 padding bytes]
Read:  Read 4 bytes, return lowest byte
```

**Methods**: `WriteByte(BYTE)`, `ReadByte()` -> `CKStateChunk.cpp:900-922`

#### Word (2 bytes -> 4 bytes stored)

```
Write: [word value (little-endian)] + [2 padding bytes]
Read:  Read 4 bytes, return lowest word
```

**Methods**: `WriteWord(WORD)`, `ReadWord()` -> `CKStateChunk.cpp:924-946`

#### Integer (4 bytes)

```
Write: [4-byte integer (little-endian)]
Read:  [4-byte integer (little-endian)]
```

**Methods**: `WriteInt(int)`, `ReadInt()` -> `CKStateChunk.cpp:948-970`

#### DWORD (4 bytes)

```
Write: [4-byte DWORD (little-endian)]
Read:  [4-byte DWORD (little-endian)]
```

**Methods**: `WriteDword(DWORD)`, `ReadDword()` -> `CKStateChunk.cpp:972-994`

#### Float (4 bytes)

```
Write: [4 bytes of float, IEEE 754 format]
Read:  [4 bytes of float, IEEE 754 format]
```

Stored via pointer cast: `*(DWORD*)&floatValue`

**Methods**: `WriteFloat(float)`, `ReadFloat()` -> `CKStateChunk.cpp:1032-1054`

#### GUID (8 bytes)

```
Write: [4 bytes Part1] + [4 bytes Part2]
Read:  [4 bytes Part1] + [4 bytes Part2]
```

GUID stored as two DWORDs (64 bits total).

**Methods**: `WriteGuid(CKGUID)`, `ReadGuid()` -> `CKStateChunk.cpp:1266-1308`

### 5.3 Complex Types

#### String

**Format**:
```
[4 bytes] Length (including null terminator)
[Length bytes] String data (null-terminated)
[Padding] Pad to DWORD boundary
```

**Examples**:
- "Hello" -> `[6][H][e][l][l][o][\0][00][00]` (6 bytes + 2 padding)
- "" -> `[1][\0][00][00]` (empty string still has length 1)

**Methods**:
- `WriteString(const char*)` -> `CKStateChunk.cpp:1196-1218`
- `ReadString(char** str)` -> `CKStateChunk.cpp:1220-1264` (allocates memory)

**Wide String**: Similar format but with 2-byte characters:
```
[4 bytes] Character count (including null)
[Count * 2] Wide character data
[Padding] Pad to DWORD boundary
```

**Methods**: `WriteWideString(wchar_t*)`, `ReadWideString()`

#### Buffer

**Format (with size)**:
```
[4 bytes] Buffer size in bytes
[Size bytes] Buffer data
[Padding] Pad to DWORD boundary
```

**Format (without size)**:
```
[Size bytes] Buffer data (caller must know size)
[Padding] Pad to DWORD boundary
```

**Methods**:
- `WriteBuffer(void* buf, int size)` -> `CKStateChunk.cpp:1092-1130`
- `WriteBufferNoSize(void* buf, int size)` -> `CKStateChunk.cpp:1132-1158`
- `ReadBuffer(void** buf, int* size)` -> `CKStateChunk.cpp:1160-1194`

#### Array (Generic)

**Format**:
```
[4 bytes] Total bytes
[4 bytes] Element count
[Total bytes] Array data
[Padding] Pad to DWORD boundary
```

**Element Size**: Computed as TotalBytes / ElementCount

**Methods**: `WriteArray_LEndian(void*, int count, int elemSize)` -> `CKStateChunk.cpp:443-487`

### 5.4 VxMath Types

#### VxVector (12 bytes)

```
[4 bytes] X component (float)
[4 bytes] Y component (float)
[4 bytes] Z component (float)
```

Stored via direct memcpy (already DWORD-aligned).

**Methods**: `WriteVector(VxVector*)`, `ReadVector()` -> `CKStateChunk.cpp:1056-1090`

#### VxMatrix (64 bytes)

```
[64 bytes] 4x4 matrix (16 floats, row-major order)
```

Stored via direct memcpy (already DWORD-aligned).

**Methods**: `WriteMatrix(VxMatrix*)`, `ReadMatrix()` -> (uses memcpy directly)

### 5.5 Object References

**Write Process**:
1. Call `WriteObjectID(CK_ID id)`
2. If object exists in file: convert ID to file index (0-based)
3. Store file index in IDs list
4. Store placeholder in main data buffer

**Read Process**:
1. Call `ReadObjectID(CK_ID* id)`
2. Read file index from IDs list
3. Look up actual object ID from m_FileObjects array
4. Return object ID

**Special Cases**:
- `-1` -> NULL reference
- `ID | 0x800000` -> Entry flagged `CK_OBJECT_ONLYFORFILEREFERENCE` (reference-only, no chunk saved)
- Negative IDs -> File references (resolved by name/class)

**Implementation**: `CKStateChunk.cpp:613-672`

---

## 6. Compression

### 6.1 Compression Algorithm

**Library**: zlib (the implementation calls `compress2` / `uncompress` via `CKPackData` and `CKUnPackData`)

**Wrappers**:
- `CKPackData(char* src, int size, int& newSize, int compressionLevel)` allocates a temporary buffer, calls `compress2`, then copies the compressed bytes into a tightly sized block.
- `CKUnPackData(int destSize, char* src, int srcSize)` allocates `destSize` bytes and feeds them to `uncompress`.

**Implementation**: `src/CKGlobals.cpp:586-618`

### 6.2 Compression Modes

| Mode                         | Value | Description                     |
|------------------------------|-------|---------------------------------|
| CKFILE_UNCOMPRESSED          | 0     | No compression                  |
| CKFILE_CHUNKCOMPRESSED_OLD   | 1     | Obsolete per-chunk compression  |
| CKFILE_WHOLECOMPRESSED       | 8     | Entire sections compressed      |

**Current Mode**: `CKFILE_WHOLECOMPRESSED` (value 8)

### 6.3 Compression Application

**Header1 Section**:
- Compressed if `FileWriteMode & CKFILE_WHOLECOMPRESSED`
- Compression level from `CKContext::GetCompressionLevel()`
- Sizes stored in `Hdr1PackSize` (compressed) and `Hdr1UnPackSize` (uncompressed)

**Data Section**:
- Compressed if `FileWriteMode & CKFILE_WHOLECOMPRESSED`
- Compression level from context
- Sizes stored in `DataPackSize` (compressed) and `DataUnPackSize` (uncompressed)

**Included Files**:
- NOT compressed, stored as raw data

**Implementation**: `CKFile.cpp:545-553, 1026-1037`

### 6.4 Compression Detection

- **Header1**: Only files written with `FileVersion >= 8` attempt to decompress the header chunk. The loader compares `Hdr1PackSize` and `Hdr1UnPackSize`; when they differ it calls `CKBufferParser::UnPack`, otherwise it keeps reading straight from the mapped file.
- **Data section**: Decompression is driven by `FileWriteMode & (CKFILE_CHUNKCOMPRESSED_OLD | CKFILE_WHOLECOMPRESSED)`. When those bits are set the loader inflates `DataPackSize` bytes into a temporary `CKBufferParser` whose lifetime ends once the manager/object chunks are parsed.
- **Packed sizes vs actual payload**: Included-file payloads are written after the packed data buffer. They are neither compressed nor part of `DataPackSize`, so the loader never attempts to inflate them.

**Implementation**: `src/CKFile.cpp:360-592`

---

## 7. Checksum Validation

### 7.1 Algorithm

- `CKComputeDataCRC` wraps zlib's `adler32` (`src/CKGlobals.cpp:600-614`). Despite the field name `Crc`, no CRC32 polynomial is used.
- The helper accepts an optional previous value so multiple segments can be streamed through a single digest.

### 7.2 Files Written With Version 8

`CKFile::EndSave` sets `header.Part0.Crc` to zero, then streams:

1. `sizeof(CKFileHeader)` bytes (Part0 + Part1 with Crc still zero)
2. `Hdr1PackSize` bytes (compressed header chunk if compression was enabled, raw otherwise)
3. `DataPackSize` bytes (compressed/uncompressed manager+object buffer)

The resulting Adler-32 value is written back to Part0. The appended include-file payload-written after the packed data buffer-is never part of the digest.

`CKFile::ReadFileHeaders` reproduces the same steps (again zeroing the stored field before hashing). Any mismatch yields `CKERR_FILECRCERROR` and aborts the load (`src/CKFile.cpp:393-407`).

### 7.3 Files With Version < 8

- No header-level digest exists. After the data section is (optionally) decompressed, the loader recomputes an Adler-32 over the remaining bytes in the parser buffer (`parser->ComputeCRC(parser->Size() - parser->CursorPos())`) and compares that value with the stored `m_FileInfo.Crc`.
- Only files with `FileVersion >= 2` take part in this check; older files simply trigger `WarningForOlderVersion`.
- Because the checksum covers only the remainder of the data buffer, appended include-file payloads and header bytes are not validated.

**Implementation**: `src/CKFile.cpp:504-520`

## 8. Object References
 Object References

### 8.1 Object Identification

**CK_ID**: 32-bit unique identifier for each object within a context

**File Index**: 0-based index into the file's object array

**Conversion**: During save, object IDs converted to file indices for serialization

### 8.2 Save Process

**Object List** (stored in Header1):
```
For each object (ObjectCount times):
    [4 bytes] Object ID (CK_ID, may have bit 23 set)
    [4 bytes] Object Class ID (CK_CLASSID)
    [4 bytes] File Index (position in data section)
    [4 bytes] Name Length (raw byte count, no terminator)
    [Length]  Name String (no terminator, no padding)
```

Strings are stored exactly as written by `CKBufferParser::Write`: the length does **not** include a null terminator and there is no padding. `ReadString` therefore allocates `len + 1` bytes, copies the raw characters, and appends its own terminator. Files with `FileVersion < 7` do not contain this table; the loader falls back to extracting the names from the serialized chunks (identifier `1`) after they have been read from the data section (`src/CKFile.cpp:608-625`).

**Object ID Encoding**:
The Object ID written to Header1 may be modified to indicate reference type:

```c
// CKFile.cpp:995-1000
if (object has CK_OBJECT_ONLYFORFILEREFERENCE flag) {
    objId |= 0x800000;  // Set bit 23 to mark as reference-only
    object->m_ObjectFlags &= ~CK_OBJECT_ONLYFORFILEREFERENCE;  // Clear flag
}
```

- **Bit 23 clear** (`objId & 0x800000 == 0`): Object fully saved in file
- **Bit 23 set** (`objId | 0x800000`): Object referenced but not fully saved (dependency only)

**Hash Table**: Built during save for ID -> File Index lookup
- Key: Object ID (original, without bit 23 modification)
- Value: File Index (0-based)
- Used by chunks to convert object ID references to file indices

**Implementation**: `CKFile.cpp:995-1000` (encoding), `CKFile.cpp:438-539, 744-780` (save)

### 8.3 Load Process

**Phase 1 - Create Objects** (`CKFile.cpp:1261-1282`):
```
For each object in file:
    1. Create object instance (CKContext::CreateObject)
    2. Store in m_FileObjects array at file index
    3. Set object name
    4. Load dynamic flags
```

**Phase 2 - Remap References** (`CKFile.cpp:1293-1302`):
```
For each object chunk:
    For each ID in chunk's IDs list:
        Convert file index -> actual object ID
        Update chunk's ID list
```

**Phase 3 - Load Data** (`CKFile.cpp:1304-1495`):
```
Specific order to handle dependencies:
    1. Non-parameter/behavior objects
    2. Local parameters
    3. Input parameters
    4. Output parameters
    5. Behaviors (last, as they reference parameters)
```

### 8.4 Reference Types

**Referenced-only Entry** (`objId & 0x800000` != 0):
```
FileIndex | 0x800000
```
The corresponding `CKObject` had `CK_OBJECT_ONLYFORFILEREFERENCE` set when the header table was built (see `src/CKFile.cpp:842-1005`). Its chunk is omitted from the data section; the ID merely documents that other entries point at it.

**Serialized Object** (`objId & 0x800000` == 0):
```
FileIndex (positive, bit 23 clear)
```
Full chunk data is stored in the data section. The file index points to the chunk's location inside the uncompressed buffer.

**NULL Reference**:
```
-1 (0xFFFFFFFF)
```

**File Reference** (negative file index):
```
Negative value
```
Reference to external file, resolved by name/class match during load.

**Note on Object ID Encoding in Header1**:
When writing the object list in Header1, objects marked with the `CK_OBJECT_ONLYFORFILEREFERENCE` flag have their ID ORed with `0x800000` before being written. This flag is then cleared from the object. During load, this bit is checked to determine if the object was fully saved or just referenced.

**Implementation**: `CKFile.cpp:998` (save), `CKFile.cpp:560, 617` (load)

### 8.5 Reference Resolution

For objects marked as file references (negative ID), the system attempts to resolve them by finding matching existing objects.

**Negative ID Workflow** (`CKFile.cpp:1273-1276`):
```c
// During Phase 5 - Object Creation
if (id >= 0) {
    // Positive ID: Create new object normally
    obj = context->CreateObject(classId, name);
    fileObjects[index] = obj;
} else {
    // Negative ID: This is a file reference, try to resolve
    fileObject->Object = -id;  // Make ID positive for storage
    obj = ResolveReference(fileObject);  // Find existing matching object
}
```

**Resolution Algorithm** (`CKFile.cpp:1553-1582`):
```c
CK_ID ResolveReference(CKFileObject* fileObj) {
    // Search for existing object matching:
    // 1. Class ID must match
    // 2. For parameters: Parameter type must match
    // 3. Name must match (case-sensitive)

    CKObject* found = context->FindObject(fileObj->Name, fileObj->ClassId);

    if (found && IsParameterClass(fileObj->ClassId)) {
        // For parameters, also verify parameter type matches
        if (GetParameterType(found) != fileObj->ParameterType) {
            found = NULL;  // Type mismatch, keep searching
        }
    }

    return found ? found->GetID() : 0;
}
```

**Use Cases**:
- **Built-in parameters**: Files may reference standard parameter types that already exist
- **Shared resources**: Objects that should not be duplicated across multiple files
- **External dependencies**: Objects defined in other loaded compositions

**Fallback**: If no matching object found, the reference remains unresolved (ID = 0/NULL)

**Implementation**: `CKFile.cpp:1273-1276` (workflow), `CKFile.cpp:1553-1582` (resolution)

---

## 9. Plugin Dependencies

### 9.1 Purpose

Tracks which plugins (DLLs) are required to load the file correctly.

**Categories**:
- `CKPLUGIN_BEHAVIOR_DLL` - Behavior building blocks
- `CKPLUGIN_MANAGER_DLL` - Manager extensions
- `CKPLUGIN_RENDERENGINE_DLL` - Render engines

### 9.2 Storage Format

```
[4 bytes] Category Count (number of plugin categories)
For each category:
    [4 bytes] Category Type (CKPLUGIN_CATEGORY)
    [4 bytes] GUID Count (number of plugins in category)
    For each GUID:
        [8 bytes] Plugin GUID (unique identifier)
```

**Location**: Header1 section, after object list

**Implementation**: `CKFile.cpp:438-539` (write), `CKFile.cpp:1089-1111` (read)

*Availability*: The writer emits this table only when `FileVersion >= 8`. For older files, `CKFile::ReadFileHeaders` can optionally rebuild a dependency list by scanning each behavior chunk during `CK_LOAD_CHECKDEPENDENCIES` (it even forces a full data read for versions < 8 to do so), but nothing is stored in the header.

### 9.3 Dependency Validation

When loading with `CK_LOAD_CHECKDEPENDENCIES` flag:

```c
For each required plugin GUID:
    if (!IsPluginRegistered(guid)) {
        MarkInvalid();
        validCount--;
    }

if (validCount < totalCount) {
    return CKERR_PLUGINSMISSING;
}
```

**User Notification**: File can still load, but behaviors may not work correctly

**Implementation**: `CKFile.cpp:1076-1129`

> **Note**: `CKFile::OpenFile` returns `CKERR_PLUGINSMISSING` when any required GUID is absent, but `CKFile::Load` treats that as a warning and continues loading so that callers can inspect the missing list afterwards.

### 9.4 Plugin Information

**Data Structure** (CKFilePluginDependencies):
```c
struct CKFilePluginDependencies {
    int m_PluginCategory;              // Category type
    XSArray<CKGUID> m_Guids;          // Array of plugin GUIDs
    XBitArray m_ValidGuids;           // Which GUIDs are registered
};
```

**Access**: `CKFile::GetMissingPlugins()` returns list of missing plugins

**Implementation**: `CKFile.h:82-110`

---

## 10. Manager Data

### 10.1 Purpose

Allows manager subsystems to save custom state/configuration data.

**Examples**:
- Attribute Manager: Attribute type definitions
- Time Manager: Global time settings
- Render Manager: Render settings

### 10.2 Storage Format

```
For each manager (ManagerCount times):
    [8 bytes] Manager GUID (unique identifier)
    [4 bytes] Chunk Size (size of following CKStateChunk)
    [Variable] CKStateChunk data (manager-specific)
```

**Location**: Beginning of Data section, before object data

**Order**: Managers loaded before objects to set up environment

*Availability*: The block is emitted/parsed only when `FileVersion >= 6` (`src/CKFile.cpp:581-612`).

**Implementation**: `CKFile.cpp:581-592` (write), `CKFile.cpp:1320-1339` (read)

### 10.3 Manager Registration

Managers must be registered in context to save/load data:

```c
// Get manager by GUID
CKBaseManager* mgr = context->GetManagerByGuid(guid);

if (mgr) {
    // Save manager data
    CKStateChunk* chunk = mgr->SaveData();
    WriteManagerData(guid, chunk);
}
```

**Unknown Managers**: If GUID not recognized during load, data is skipped

**Implementation**: `CKFile.cpp:1320-1339`

### 10.4 Data Structure

**CKFileManagerData**:
```c
struct CKFileManagerData {
    CKGUID Manager;           // Manager GUID (8 bytes)
    CKStateChunk* data;       // Manager's state chunk
};
```

**Storage**: Array `m_ManagersData` in CKFile

**Implementation**: `CKFile.h:113-120`

---

## 11. Included Files

### 11.1 Purpose

Allows embedding external resources (textures, sounds, scripts, etc.) directly inside the Virtools composition produced by `CKFile::EndSave`.

### 11.2 On-disk Layout

1. **Header stub (never populated)**: `ReadFileHeaders` expects Header1 to contain an `includedFileSize` followed by an `includedFileCount` and a directory of names. The current writer never emits that data, so the loader always sees zero entries (`src/CKFile.cpp:463-470`, `src/CKFile.cpp:900-1035`).
2. **Payload after the data buffer**: After the packed/unpacked data section has been written, `EndSave` appends each requested file as:
   ```
   [4 bytes]  Name length (no terminator, no padding)
   [n bytes]  Filename (as returned by `CKJustFile`)
   [4 bytes]  File size
   [size]     Raw bytes, uncompressed
   ```
   There is no sentinel or overall size; the stream simply contains one record per `CKFile::IncludeFile` call (`src/CKFile.cpp:1127-1154`).

### 11.3 Load-time Behaviour (current limitations)

- `CKFile::ReadFileHeaders` reads the stub fields but, because the writer never filled them, `m_IncludedFiles` stays empty.
- Even if metadata were present, `ReadFileData` attempts to read the payload from the decompressed data buffer (`parser->ReadInt()` / `parser->ExtractFile()`), while the real bytes live after the packed data region on disk. The temporary parser is deleted before those bytes could be accessed (`src/CKFile.cpp:545-690`).
- As a result, files appended via `CKFile::IncludeFile` cannot be recovered when the composition is loaded-the payload remains at the end of the .cmo/.nmo file but is never extracted.

### 11.4 Save-time Workflow

```c
CKBOOL CKFile::IncludeFile(CKSTRING filename, int category) {
    if (CKPathManager::ResolveFileName(...) == CK_OK)
        m_IncludedFiles.PushBack(resolvedPath);
}
```

During `EndSave` the engine iterates `m_IncludedFiles`, writes the unqualified filename, the file size, and the raw bytes to the disk stream, then clears the list (`src/CKFile.cpp:1127-1154`). No CRC or size metadata is updated afterwards.

## 12. Load/Save Workflows

### 12.1 Save Workflow

1. **Preparation**: `StartSave` clears previous state, resolves the output file, warns behaviors (`CKM_BEHAVIORPRESAVE`), and builds the list of objects/managers to serialize.
2. **Object serialization**: Every object in `m_FileObjects` gets `Save`d into a `CKStateChunk`. Behaviors also request interface chunks through the UI callback.
3. **Manager data**: Managers with non-null `SaveData` chunks are collected (only when saving with FileVersion >= 6).
4. **Header1 build**: A contiguous buffer is filled with object descriptors (ID/class/file offset/name) and, for FileVersion >= 8, the plugin dependency table. Eight additional bytes are reserved for an included-file directory but remain zero because `EndSave` never populates them. If `FileWriteMode` enables compression the buffer is passed through `CKPackData`.
5. **Data buffer build**: Manager chunks are written first, followed by `[chunkSize][chunk]` pairs for each object. The buffer may be compressed depending on `FileWriteMode`.
6. **Checksum + write**: An Adler-32 is streamed over Part0/Part1 (with Crc cleared), Header1, and the packed data buffer. The header, Header1, and Data buffers are written in order, after which every file supplied via `IncludeFile` is appended as `[nameLen][name][fileSize][bytes]`. This trailing payload is not covered by the checksum nor counted in `m_FileInfo.FileSize`.
7. **Cleanup**: Temporary buffers are freed, managers receive `ExecuteManagersPostSave`, and `m_IncludedFiles` is cleared.

### 12.2 Load Workflow

1. **OpenFile/OpenMemory**: Map the file, verify the `"Nemo Fi "` signature, and keep a `CKBufferParser` over the entire byte range.
2. **Header parsing**: `ReadFileHeaders` reads Part0 (always) and Part1 (when present). Files with `FileVersion >= 8` have their Header1 chunk decompressed if `Hdr1PackSize != Hdr1UnPackSize`, then the Adler-32 is recomputed over Header/Header1/Data and compared against `Crc`. Files with newer versions (>=10) are rejected.
3. **Object index + plugins**: For `FileVersion >= 7` the object descriptors are read into `m_FileObjects`. When `FileVersion >= 8` the plugin dependency block is parsed and validated. For older files, dependencies are only checked if `CK_LOAD_CHECKDEPENDENCIES` was requested and, if needed, the loader performs a full data read to scan behavior chunks.
4. **Included-file metadata**: The loader attempts to read the reserved stub but, because the writer never emits it, `m_IncludedFiles` stays empty.
5. **Data extraction**: `ReadFileData` decompresses the data buffer when required, reads manager chunks (v6+), then object chunks (v4+). For very old files it falls back to the legacy per-object format. The function also contains an extraction loop for included files, but it never runs because the metadata count is zero and the appended payload is outside the temporary parser.
6. **Finish loading**: After `ReadFileData` the mapped file/parsers are released and `FinishLoading` recreates objects, remaps references, runs `Load` on each chunk (in the parameter/behavior order implemented in `src/CKFile.cpp:1261-1495`), and calls manager post-load hooks.
7. **Plugin warnings**: If `ReadFileHeaders` detected missing plugins, `OpenFile` returns `CKERR_PLUGINSMISSING`; `CKFile::Load` treats that as a warning and continues to call `LoadFileData` so callers can both inspect the missing list and operate on the partial load.

### 12.3 Detailed Load Algorithms

**`ReadFileHeaders` pseudo-code (`src/CKFile.cpp:343-520`):**

```text
read Part0
if Part0.FileVersion2 != 0:
    zero Part0, set WarningForOlderVersion

if Part0.FileVersion >= 10: return CKERR_OBSOLETEVIRTOOLS
if Part0.FileVersion >= 5 and buffer >= 64 bytes: read Part1 else zero Part1
normalize ProductVersion >= 12 -> (0, 0x1010000)
populate m_FileInfo

if FileVersion >= 8:
    recompute Adler-32 over (Part0 with Crc=0) + Part1 + Header1 + Data
    compare with stored Crc -> CKERR_FILECRCERROR on mismatch
    if Hdr1PackSize != Hdr1UnPackSize: parser = parser->UnPack(...)

if FileVersion >= 7:
    read object descriptors (ID, class, file offset, name bytes)

if FileVersion >= 8:
    read plugin dependency block and validate GUIDs when CK_LOAD_CHECKDEPENDENCIES
    read included-file stub (always zero with current writer)

if FileVersion < 8 and CK_LOAD_CHECKDEPENDENCIES:
    call ReadFileData immediately to rebuild dependency info
```

**`ReadFileData` pseudo-code (`src/CKFile.cpp:520-703`):**

```text
parser = *ParserPtr
if FileWriteMode uses compression:
    unpack data section and advance original parser cursor

if FileVersion < 8:
    verify CRC on remaining bytes
    read SaveIDMax/ObjectCount when header did not provide them

if FileVersion >= 6:
    read manager GUID + chunkSize; store CKFileManagerData entries

read object chunks:
    legacy (<4) use old layout (classId, saveFlags, packed size)
    >=4 read [chunkSize][chunk bytes], optionally skip when CK_LOAD_ONLYBEHAVIORS is set

attempt to read included files (never executed today because header stub is empty)
delete temporary parser if one was allocated
```

**`FinishLoading` ordering (`src/CKFile.cpp:1238-1587`):**

1. Create inclusion/exclusion masks from `g_CKClassInfo`.
2. `CKObjectManager::StartLoadSession(m_SaveIDMax + 1)`.
3. For every descriptor:
   - Create object via `CKContext::CreateObject`, or resolve references for negative IDs.
   - Register load object and cache pointers/IDs.
   - Push file indices into `m_IndexByClassId`.
4. Update context version info when levels are present.
5. Remap IDs in chunks and manager data unless `CK_LOAD_ONLYBEHAVIORS` is set.
6. Load phases: non-parameter beobjects -> local params -> input params -> output params -> standard params -> behaviors.
7. Apply owner/patch fix-ups and issue UI progress callbacks.
8. Run manager `LoadData`, behavior callbacks (`CKM_BEHAVIORLOAD`), and `CKObject::PostLoad`.
9. `CKObjectManager::EndLoadSession`.

### 12.4 Detailed Save Algorithms

**`StartSave` (`src/CKFile.cpp:706-780`):**

```text
ClearData()
ExecuteManagersPreSave()
m_Context->m_Saving = TRUE
m_Flags = Flags
m_IndexByClassId.resize(maxClassId)
touch destination file to ensure it is writable
Warn behaviors (CKM_BEHAVIORPRESAVE)
```

**`EndSave` outline (`src/CKFile.cpp:840-1157`):**

```text
mark referenced objects with CK_OBJECT_ONLYFORFILEREFERENCE
collect scripted behaviors lacking interface chunks; invoke UI callback to build them
for each CKFileObject:
    chunk = obj->Save(this, flags)
    compute Pre/PostPackSize
collect manager data (SaveData != null)
CKPluginManager::ComputeDependenciesList(this)
compute header sizes (object info, plugin deps, manager data)
write Header1 (object list + plugin deps + reserved include-file block)
write Data buffer (manager chunks first, then object chunks)
optionally compress both buffers and update Part0/Part1 sizes
compute Adler-32 over Part0/Part1/Header1/Data and store in Part0.Crc
write headers + packed buffers to disk
append included files: for each filename write [nameLen][name][fileSize][blob]
clear temporary buffers, included file list, and execute managers post-save
```

### 12.5 Error Propagation & Recovery

- `CKERR_PLUGINSMISSING` is a warning: the load completes but the caller must inspect `GetMissingPlugins`.
- `CKERR_FILECRCERROR`, `CKERR_INVALIDFILE`, and `CKERR_OBSOLETEVIRTOOLS` abort the load. `CKFile::LoadFileData` resets automatic load/user callbacks before returning the error.
- `CKERR_CANTWRITETOFILE` and `CKERR_NOTENOUGHDISKPLACE` bubble up from `EndSave` when the destination cannot be opened or flushed.
- `CKBufferParser::UnPack` failures produce console messages ("Error unpacking header/data chunk") and return `CKERR_INVALIDFILE`.
- `WarningForOlderVersion` simply logs "Obsolete File Format, Please Re-Save..." after successful loads so users can upgrade their assets.


## 13. Constants Reference
 Constants Reference

### 13.1 File Versions

```c
#define CURRENT_FILE_VERSION    8
#define MIN_FILE_VERSION        2
#define MAX_FILE_VERSION        9
```

### 13.2 FileWriteMode Flags

```c
#define CKFILE_UNCOMPRESSED             0   // No compression
#define CKFILE_CHUNKCOMPRESSED_OLD      1   // Obsolete (per-chunk compression)
#define CKFILE_EXTERNALTEXTURES_OLD     2   // Obsolete (external textures)
#define CKFILE_FORVIEWER                4   // Exclude editor data
#define CKFILE_WHOLECOMPRESSED          8   // Compress entire sections
```

**Usage**:
```c
fileWriteMode = CKFILE_WHOLECOMPRESSED | CKFILE_FORVIEWER;
```

### 13.3 Load Flags

```c
#define CK_LOAD_ANIMATION             0x00000001  // Load animations
#define CK_LOAD_GEOMETRY              0x00000002  // Load geometry
#define CK_LOAD_ASCHARACTER           0x00000004  // Wrap in character
#define CK_LOAD_DODIALOG              0x00000008  // Show duplicate dialog
#define CK_LOAD_AS_DYNAMIC_OBJECT     0x00000010  // Create as dynamic
#define CK_LOAD_AUTOMATICMODE         0x00000020  // Auto-rename duplicates
#define CK_LOAD_CHECKDUPLICATES       0x00000040  // Check name uniqueness
#define CK_LOAD_CHECKDEPENDENCIES     0x00000080  // Validate plugins
#define CK_LOAD_ONLYBEHAVIORS         0x00000100  // Load only behaviors
```

**Usage**:
```c
loadFlags = CK_LOAD_GEOMETRY | CK_LOAD_CHECKDEPENDENCIES;
```

### 13.4 Chunk Version

```c
#define CHUNK_VERSION1          4       // Obsolete (legacy format)
#define CHUNK_VERSION2          5       // Obsolete (legacy format)
#define CHUNK_VERSION3          6       // Obsolete (legacy format)
#define CHUNK_VERSION4          7       // Current version
```

**Note**: The constant names do NOT match the constant values. CHUNK_VERSION1 has value 4, not 1. These are historical names from the Virtools SDK. All new chunks are written with CHUNK_VERSION4 (value 7).

### 13.5 Chunk Options

```c
// Serialized options (saved in version info)
#define CHNK_OPTION_IDS         0x01    // Contains object IDs
#define CHNK_OPTION_MAN         0x02    // Contains manager refs
#define CHNK_OPTION_CHN         0x04    // Contains sub-chunks
#define CHNK_OPTION_FILE        0x08    // File context (indices vs IDs)
#define CHNK_OPTION_ALLOWDYN    0x10    // Dynamic object can be written

// Runtime-only options (not saved to file)
#define CHNK_OPTION_LISTBIG     0x20    // Lists stored in big endian
#define CHNK_DONTDELETE_PTR     0x40    // Data buffer not owned by chunk
#define CHNK_DONTDELETE_PARSER  0x80    // Parser not owned by chunk
```

### 13.6 Plugin Categories

```c
#define CKPLUGIN_BEHAVIOR_DLL           0   // Behavior building blocks
#define CKPLUGIN_MANAGER_DLL            1   // Manager extensions
#define CKPLUGIN_RENDERENGINE_DLL       2   // Render engines
```

---

## 14. Implementation Notes

### 14.1 Memory Management

**Buffer Allocation**:
- `CKStateChunk` stores its payload in `m_Data` and grows it via `CheckSize`.
- The allocator adds capacity in blocks of at least 500 DWORDs; there is no `CHNK_OPTION_ALLOWRESIZE`.
- Initial allocation happens lazily the first time `CheckSize` is invoked (roughly 1 KB).

**Memory Mapping**:
- Large files use `VxMemoryMappedFile` for efficient loading
- Reduces memory footprint
- Automatic cleanup

**Implementation**: `CKFile.cpp:211-217, 282-289`

### 14.2 Performance Considerations

**Compression Trade-offs**:
- Pro: Smaller file size (often 50-70% reduction)
- Con: Slower load/save times
- Recommendation: Use for distribution, not during development

**Memory-Mapped I/O**:
- Pro: Faster loading, less memory
- Con: Requires contiguous virtual address space
- Recommendation: Always use for files > 10MB

**Chunk Pre-allocation**:
- CKStateChunk can pre-allocate buffer size
- Reduces reallocation during write
- Recommendation: Estimate size based on object complexity

### 14.3 Compatibility

**Backward Compatibility**:
- Current implementation reads versions 2-9
- Older versions may have limited feature support
- Warning issued for version < 2

**Forward Compatibility**:
- Files with version >= 10 rejected
- Prevents loading files from future versions
- Ensures data integrity

### 14.4 Security Considerations

**Checksum coverage**:
- The only integrity guard is the Adler-32 described in Section 7. Version < 8 files skip the header entirely and even version 8+ ignores the appended include-file payload.

**Parser safety**:
- `CKBufferParser::Read`, `ReadString`, and similar helpers use raw `memcpy` without bounds checks. A malformed cursor can therefore overrun buffers before the code notices.
- `CKStateChunk::ConvertFromBuffer` trusts the counts stored in the stream before allocating arrays.

**Practical impact**:
- Invalid signatures, absurd object counts, or impossible versions are rejected early, but otherwise the loader assumes trusted input. Treat files from untrusted sources with caution.

### 14.5 Chunk Versioning Details

**Automatic Version Assignment**:
```c
// CKStateChunk.cpp:146
void CKStateChunk::StartWrite() {
    m_ChunkVersion = CHUNK_VERSION4;  // Always set to 7 for new chunks
    // ... other initialization
}
```

All newly written chunks automatically use `CHUNK_VERSION4` (value 7), regardless of the file version. This ensures consistency in the chunk format.

**Legacy Version Support**:
The chunk reader supports multiple legacy versions:
- `CHUNK_VERSION1` (value 4): Basic format
- `CHUNK_VERSION2` (value 5): Added enhanced features
- `CHUNK_VERSION3` (value 6): Further improvements
- `CHUNK_VERSION4` (value 7): Current format with all features

**ChunkClassID Usage**:
While often described as "deprecated," the `ChunkClassID` field is actively used in the implementation:
- Stored in bits 8-15 of the version info DWORD
- Read and written during chunk serialization (`CKStateChunk.cpp:1333, 1365, 1465`)
- Used for backward compatibility with older chunk formats
- Modern code typically sets this to 0, but legacy code may use it for class identification

The field is preserved to maintain compatibility with older Virtools content.

### 14.6 Debugging Support

**Logging**:
- `CKFile` reports issues through `CKContext::OutputToConsole` / `OutputToConsoleEx`.

**Validation**:
- Adler-32 (v8+) and plugin checks are the only automated guards; everything else relies on higher-level context policies.

---

## 15. Appendix

### 15.1 File Extension Conventions

- `.nmo` - Nemo composition file (standard Virtools file)
- `.cmo` - Virtools composition file (legacy)
- `.vmo` - Virtools media object

### 15.2 Typical File Sizes

| Content Type          | Uncompressed | Compressed | Ratio |
|-----------------------|--------------|------------|-------|
| Simple scene (10 obj) | 50-100 KB    | 20-40 KB   | ~40%  |
| Medium scene (100 obj)| 500 KB-2 MB  | 200-800 KB | ~40%  |
| Complex scene (1000+) | 5-20 MB      | 2-8 MB     | ~40%  |
| With textures         | 50-500 MB    | 10-100 MB  | ~20%  |

### 15.3 Common Object Class IDs

| Class Name          | Class ID    | Description              |
|---------------------|-------------|--------------------------|
| CKObject            | 0x00000001  | Base object              |
| CKBeObject          | 0x00000002  | Behavioral object        |
| CKSceneObject       | 0x00000003  | Scene object             |
| CK3dEntity          | 0x00000004  | 3D entity                |
| CK3dObject          | 0x00000005  | 3D object (renderable)   |
| CKCamera            | 0x00000006  | Camera                   |
| CKLight             | 0x00000007  | Light                    |
| CKMesh              | 0x00000008  | 3D mesh                  |
| CKMaterial          | 0x00000009  | Material                 |
| CKTexture           | 0x0000000A  | Texture                  |
| CKBehavior          | 0x00000014  | Behavior script          |
| CKParameter         | 0x00000015  | Parameter                |
| CKParameterIn       | 0x00000016  | Input parameter          |
| CKParameterOut      | 0x00000017  | Output parameter         |
| CKParameterLocal    | 0x00000018  | Local parameter          |

### 15.4 Tools and Utilities

**Reading Virtools Files**:
- Virtools Dev (official tool)
- Custom parsers using this specification

**Inspecting File Contents**:
- Hex editor for raw binary inspection
- Adler-32 calculator for validation (for v8+ files)
- Decompression tools (zlib) for compressed sections

### 15.5 References

**Source Code**:
- `include/CKFile.h` - CKFile class declaration
- `src/CKFile.cpp` - File I/O implementation
- `include/CKStateChunk.h` - CKStateChunk declaration
- `src/CKStateChunk.cpp` - Serialization implementation
- `include/CKEnums.h` - Constants and enumerations

**External Libraries**:
- zlib: https://zlib.net (compression + Adler-32 checksum)

---

## Revision History

| Version | Date       | Changes                                                                      |
|---------|------------|------------------------------------------------------------------------------|
| 1.0     | 2025-11-09 | Initial comprehensive specification                                          |
| 1.1     | 2025-11-09 | **Critical corrections**: Fixed chunk version packing (added ChunkOptions in bits 24-31), corrected reference object ID flag (0x800000 not 0x80000000), fixed chunk version constants (4,5,6,7 not 1,2,3,7), corrected chunk option names (ALLOWDYN not ALLOWRESIZE), added missing chunk options (LISTBIG, DONTDELETE_PTR, DONTDELETE_PARSER). **Enhancements**: Added object ID encoding details in Header1, expanded negative ID resolution workflow, clarified included files size field, added chunk versioning implementation notes |
| 1.2     | 2025-11-09 | Reconciled the spec with the current CK2 sources: documented the real magic bytes, Adler-32 checksum flow, plugin/version gating, broken included-file workflow, and the absence of logging/bounds checks inside `CKFile`. Updated compression/library references and overhauled the load/save walkthrough to match `src/CKFile.cpp`. |

---

**End of Specification**
