# Virtools File Format Implementation Guide (v1.0)

> Use this document alongside `VIRTOOLS_FILE_FORMAT_SPEC.md`. The spec describes **what** is stored on disk; this guide explains **how** the runtime produces and consumes every byte. The goal is to implement a drop-in compatible loader/saver without access to the proprietary CK2 sources.

---

## 0. Reading Order

1. Read the spec once to understand the binary layout.
2. Come back here for the algorithms, API contracts, and runtime side-effects.
3. Keep the CK headers (`include/CK*.h`) handy to mirror exact structures.

---

## 1. Runtime Architecture Overview

| Component | Responsibilities | Key Files |
|-----------|------------------|-----------|
| `CKFile` | Owns load/save workflows, manages buffers, dependency checks, plugin bookkeeping, and context integration. | `include/CKFile.h`, `src/CKFile.cpp` |
| `CKStateChunk` | Serializes individual object/manager state, tracks object IDs, nested chunks, manager ints, bitmap payloads. | `include/CKStateChunk.h`, `src/CKStateChunk.cpp` |
| `CKBufferParser` | Cursor-based view over memory/mapped files; supplies compression (`CKPackData`/`CKUnPackData`), CRC streaming, and file extraction. | `include/CKFile.h`, `src/CKFile.cpp` |
| `CKObjectManager` | Allocates global IDs, maintains per-class lists, tracks load sessions, handles dynamic objects and app data. | `include/CKObjectManager.h`, `src/CKObjectManager.cpp` |
| `CKContext` | Wraps manager callbacks, duplicate-name policies, plugin dispatching, and exposes the `CKFileInfo` metadata helpers. | `include/CKContext.h`, `src/CKContext.cpp` |

A compatible implementation must offer equivalent APIs so that existing Virtools assets continue to work.

---

## 2. Core Runtime Structures

### 2.1 CKFile Working Sets

| Structure | Fields (summary) | Notes |
|-----------|------------------|-------|
| `CKFileObject` | `Object`, `CreatedObject`, `ObjectCid`, `ObjPtr`, `Name`, `Data`, `PostPackSize`, `PrePackSize`, `Options`, `FileIndex`, `SaveFlags` | Created both for serialized objects and reference-only entries. `Options` is set to `CK_FO_RENAMEOBJECT` when duplicate names are detected. |
| `CKFileManagerData` | `CKGUID Manager`, `CKStateChunk* data` | Holds manager state; `data` can be null if the manager chose not to save. |
| `CKFilePluginDependencies` | `m_PluginCategory`, `m_Guids`, `ValidGuids` | `ValidGuids` is an `XBitArray` mirroring the GUID array. |
| `CKFileInfo` | Mirrors Part0/Part1 header data (product version/build, CK version, file version, write mode, counts, CRC, packed/unpacked sizes). | Filled either by `ReadFileHeaders` or `CKContext::GetFileInfo`. |

### 2.2 CKStateChunk Bookkeeping

- `m_Data` – DWORD-aligned payload grown via `CheckSize` in blocks of at least 500 DWORDs.
- `m_Ids`, `m_Chunks`, `m_Managers` – `IntListStruct` instances recording offsets to object IDs, sub-chunks, and manager ints respectively.
- `m_ChunkVersion` – always set to `CHUNK_VERSION4` when writing new chunks; legacy chunks are normalized when read.
- `m_ChunkOptions` – packed into the high byte of the version word to indicate which optional lists exist and whether IDs were written relative to a file (`CHNK_OPTION_FILE`).

---

## 3. CKFile Lifecycle & Public API

### 3.1 Creation and Reset

```text
CKFile::CKFile(Context): initialize masks, pointers, counters.
ClearData():
    - Delete leftover CKStateChunks, names, parsers, mapped files.
    - Reset arrays (FileObjects, ManagersData, IncludedFiles, bitmasks).
    - Zero `m_FileInfo`, `m_SaveIDMax`, flags, file name.
```

`ClearData` is invoked before every new load/save sequence to avoid leaking stale pointers.

### 3.2 Loading Pipeline

```text
CKERROR CKFile::Load(filename, list, flags):
    err = OpenFile(filename, flags)
    if err != CK_OK && err != CKERR_PLUGINSMISSING: return err
    m_Context->SetLastCmoLoaded(filename)
    return LoadFileData(list)
```

1. **`OpenFile`** – map file via `VxMemoryMappedFile` and forward to `OpenMemory`.
2. **`OpenMemory`** – sanity-check buffer size and signature ("Nemo Fi\0"), allocate `CKBufferParser`, store flags, resize `m_IndexByClassId`, and call `ReadFileHeaders`.
3. **`ReadFileHeaders`** – parse Part0/Part1, validate versions, compute Adler-32 for v8+, load object descriptors (v7+), plugin dependencies (v8+), and the unused included-file stub. For pre-v8 files with `CK_LOAD_CHECKDEPENDENCIES`, immediately call `ReadFileData` to rebuild the dependency list.
4. **`ReadFileData`** – decompress sections when `FileWriteMode` requires it, validate legacy CRCs, read manager data (v6+), object chunks (v4+), and attempt to extract included files (no-op today because the stub is empty).
5. **`LoadFileData`** – executes `ReadFileData` if it has not already run, frees parsers/mapped files, calls `FinishLoading`, and restores context callbacks/automatic load mode.
6. **`FinishLoading`** – performs the heavy lifting: creates runtime objects (respecting duplicate policies), registers them with the object manager, remaps IDs inside every chunk, runs load passes per class type (non-parameter objects → parameters → behaviors), executes manager `LoadData`, behavior callbacks (`CKM_BEHAVIORLOAD`), and finally ends the load session.

#### Duplicate Handling & Flags

- `CK_LOAD_DODIALOG` / `CK_LOAD_AUTOMATICMODE` / `CK_LOAD_CHECKDUPLICATES` switch `CreateObject` into `CK_OBJECTCREATION_ASK`, allowing the context to rename or reuse existing objects.
- `CK_LOAD_AS_DYNAMIC_OBJECT` adds `CK_OBJECTCREATION_DYNAMIC`; such objects are tracked in `m_DynamicObjects` and can later be removed via `DeleteAllDynamicObjects`.
- `CK_LOAD_ONLYBEHAVIORS` skips remapping and loading for non-behavior classes to support lightweight script extraction.

#### Reference Resolution

- Header1 marks reference-only entries by OR-ing the saved ID with `0x800000`. `FinishLoading` clears the bit and skips chunk loading for those entries.
- Negative IDs indicate an external reference (currently only implemented for parameters). `ResolveReference` looks up by name + parameter type and stores the resolved ID back into the descriptor.

### 3.3 Saving Pipeline

1. **`StartSave`** – calls `ClearData`, `ExecuteManagersPreSave`, sets `m_Context->m_Saving = TRUE`, stores the output file path, validates it by opening/closing the file, and notifies behaviors via `CKM_BEHAVIORPRESAVE`.
2. **`SaveObject` / `SaveObjectAsReference`** – populate `m_FileObjects`, `m_ObjectsHashTable`, `m_IndexByClassId`, and update `m_SaveIDMax`. Reference-only objects are tracked in `m_ReferencedObjects` so `EndSave` can set `CK_OBJECT_ONLYFORFILEREFERENCE` before serialization.
3. **`EndSave`** –
   - Marks referenced objects with `CK_OBJECT_ONLYFORFILEREFERENCE`.
   - Requests interface chunks for scripted behaviors lacking them.
   - Calls `obj->Save()` on every entry, capturing `CKStateChunk`s along with pre/post compression sizes.
   - Collects manager chunks via `manager->SaveData()`.
   - Asks `CKPluginManager::ComputeDependenciesList` to refresh `m_PluginsDep`.
   - Builds Header1 (object descriptors + plugin deps + reserved include-file directory).
   - Builds the data buffer (manager chunks followed by `[packSize][chunkBytes]` per object).
   - Compresses Header1/Data when `FileWriteMode` uses `CKFILE_WHOLECOMPRESSED` or `CKFILE_CHUNKCOMPRESSED_OLD`.
   - Streams Adler-32 across Part0/Part1/Header1/Data, writes everything to disk, and appends included files as `[nameLen][nameBytes][size][raw bytes]`.
   - Executes manager post-save callbacks, clears temporary buffers, and resets the flags.

4. **`LoadAndSave`** – helper that loads a file (with `CK_LOAD_DEFAULT`), then re-saves it with the current runtime’s settings.

---

## 4. CKStateChunk Implementation Details

### 4.1 Buffer Growth & Version Packing

- `StartWrite` deletes previous data, resets the parser cursor, and sets `m_ChunkVersion = CHUNK_VERSION4`.
- `CheckSize(bytes)` grows `m_Data` by copying into a new buffer whose size increases by `max(sz, 500)` DWORDs. Every read/write helper assumes this precondition.
- `ConvertToBuffer` emits `[versionInfo][chunkSize][payload][optional lists]`, where
  - `versionInfo = (m_DataVersion | (m_ChunkClassID << 8)) | ((m_ChunkVersion | (chunkOptions << 8)) << 16)`.
  - `chunkOptions` contains `CHNK_OPTION_IDS`, `CHNK_OPTION_CHN`, `CHNK_OPTION_MAN`, `CHNK_OPTION_FILE`, and `CHNK_OPTION_ALLOWDYN`.
- `ConvertFromBuffer` detects legacy layouts (`CHUNK_VERSION < 5`), reads the old format (idCount/chunkCount stored explicitly), and normalizes the result into the modern in-memory representation.

### 4.2 Object ID & Sequence APIs

- `WriteObjectID` converts `CK_ID` -> file index via `CKFile::SaveFindObjectIndex` when `m_File` is set and `CHNK_OPTION_FILE` is active; otherwise it stores the raw ID.
- `StartObjectIDSequence(count)` reserves a count slot and pushes the cursor position into `m_Ids`. Matching `StartReadSequence` returns the same count on load.
- `StartSubChunkSequence` / `WriteSubChunkSequence` and `StartManagerSequence` / `WriteManagerSequence` follow the same pattern for nested chunks and manager integer tables.

### 4.3 Remapping & Iteration

- `ChunkIteratorData` holds pointers to the payload, ID list, chunk list, manager list, conversion tables, and optional dependency contexts.
- `IterateAndDo` recursively walks sub-chunks, invoking callbacks such as `ObjectRemapper`, `ManagerRemapper`, or `ParameterRemapper`.
- `RemapObjects(CKContext*)` is called once all runtime objects exist; it rewrites every file index into the correct `CK_ID` (including nested chunks).
- `RemapManagerInt` and `RemapParameterInt` apply conversion tables provided by managers or parameter types.

### 4.4 Bitmap & Raw Image Pipelines

- `WriteRawBitmap` / `ReadRawBitmap` store width, height, pitch, format, and raw byte data—useful when no plugin involvement is desired.
- `WriteBitmap` / `ReadBitmap` normalize `BITMAP_HANDLE`s into `VxImageDescEx` structures, write metadata, and copy raw pixels; on read the bitmap is recreated (allocating a handle if necessary).
- `WriteReaderBitmap` / `ReadReaderBitmap` serialize the bitmap reader GUID, optional `CKBitmapProperties`, and the compressed stream produced by the plugin designated by the stored extension.
- All bitmap routines share the same layout and keep the buffer DWORD-aligned.

---

## 5. CKBufferParser & Compression Layer

- `Write`/`Read` – raw memcpy with cursor advancement; bounds must be ensured by the caller.
- `Seek`/`Skip` – absolute or relative cursor movement.
- `Extract(size)` – copies `size` bytes into a new parser instance.
- `ExtractChunk(size, file)` – wraps `Extract` and feeds the bytes to `CKStateChunk::ConvertFromBuffer`.
- `Pack(size, level)` / `UnPack(unpackSize, packSize)` – wrappers over `CKPackData` (`compress2`) and `CKUnPackData` (`uncompress`).
- `ComputeCRC(size, prev)` – feeds the next `size` bytes to `CKComputeDataCRC` (Adler-32) and returns the updated checksum.
- `ExtractFile(filename, size)` – dumps the next `size` bytes to disk; used when appending included files after the packed data buffer.

A faithful implementation must match these semantics because the higher-level algorithms assume cursor-based random access with optional compression.

---

## 6. CKObjectManager & CKContext Coordination

### 6.1 Load Session Contract

1. `StartLoadSession(maxSavedId + 1)` – reserves a block of IDs so newly created objects never reuse IDs saved in the file.
2. `RegisterLoadObject(obj, savedId)` – records the mapping so `CKFile::LoadFindObject(fileIndex)` can return the runtime ID later.
3. `EndLoadSession()` – releases temporary tables and finalizes ID assignments.

### 6.2 Duplicate Handling & Automatic Mode

- `CKContext::SetAutomaticLoadMode` controls how duplicates are resolved: rename, reuse, or keep existing objects. `CKFile` resets the mode to `CKLOAD_INVALID` after every load attempt.
- UI callbacks (`m_Context->m_UICallBackFct`) receive `CKUIM_LOADSAVEPROGRESS` updates during load/save loops and can abort the process if needed.

### 6.3 Dynamic Objects

- When `CK_LOAD_AS_DYNAMIC_OBJECT` is set, `CKObjectManager::SetDynamic` marks every created object as dynamic. `DeleteAllDynamicObjects` later iterates `m_DynamicObjects` to purge them without affecting the original project.

### 6.4 Manager Hooks

| Hook | When Called |
|------|-------------|
| `ExecuteManagersPreLoad` / `PostLoad` | Surround `ReadFileData`/`FinishLoading`. |
| `ExecuteManagersPreSave` / `PostSave` | Surround the entire save pipeline. |
| `manager->SaveData` / `LoadData` | During `EndSave` and `FinishLoading`, respectively. |

Managers are expected to return `CKStateChunk*` objects and consume them symmetrically.

---

## 7. Plugin Dependencies

1. During save, `CKPluginManager::ComputeDependenciesList(this)` populates `m_PluginsDep` with each required GUID grouped by category (`CKPLUGIN_BEHAVIOR_DLL`, `CKPLUGIN_MANAGER_DLL`, etc.).
2. During load, `ReadFileHeaders` reads the block and, when `CK_LOAD_CHECKDEPENDENCIES` is active, calls `CKPluginManager::FindComponent`. Missing GUIDs stay unset in `ValidGuids` and set a flag so `OpenFile` returns `CKERR_PLUGINSMISSING`.
3. `CKFile::GetMissingPlugins()` exposes the array so callers can surface useful error messages.

---

## 8. Included Files Pipeline & Limitation

- `CKFile::IncludeFile(path)` resolves the file through `CKPathManager`. Resolved paths are stored verbatim in `m_IncludedFiles`.
- `EndSave` appends each file directly after the packed data section:
  ```text
  [4 bytes] name length (no terminator)
  [n bytes] filename (as returned by `CKJustFile`)
  [4 bytes] file size
  [size bytes] raw payload
  ```
- **Limitation**: The metadata stub in Header1 is never filled, so `ReadFileData` never learns how many files were appended and therefore cannot extract them. To support this feature, extend Header1 with file descriptors while remaining backward compatible (e.g., reserve a GUID-tagged block) and teach `ReadFileData` to read past the data section in the mapped file. Until then, included data is write-only.

---

## 9. Backward Compatibility Matrix

| File Version | Notable Features |
|--------------|------------------|
| 2–3 | Legacy header layout (no Part1), CRC over data section only. |
| 4 | Modern chunk layout for objects (size-prefixed payloads). |
| 5 | Header Part1 introduced (manager/object counts, packed sizes). |
| 6 | Manager data serialization enabled. |
| 7 | Object descriptor table moved into Header1. |
| 8 | Adler-32 over header/data, plugin dependency table, included-file stub. |
| ≥10 | Rejected (`CKERR_OBSOLETEVIRTOOLS`). |

`WarningForOlderVersion` is set whenever legacy constructs are encountered; after loading the engine prints "Obsolete File Format, Please Re-Save...".

---

## 10. Error Handling & Recovery

| Error Code | Trigger | Recovery |
|------------|--------|----------|
| `CKERR_INVALIDFILE` | Bad signature, truncated sections, corrupt sizes, unpack failures. | Abort load/save; caller must inspect logs. |
| `CKERR_OBSOLETEVIRTOOLS` | FileVersion ≥ 10. | Abort load; user must upgrade the engine. |
| `CKERR_FILECRCERROR` | Adler-32 mismatch. | Abort load. |
| `CKERR_PLUGINSMISSING` | Dependency not registered. | Load continues but returns the warning so callers can inspect `GetMissingPlugins`. |
| `CKERR_CANTWRITETOFILE` | Destination cannot be opened. | Abort save. |
| `CKERR_NOTENOUGHDISKPLACE` | fwrite failure. | Abort save; caller should delete partial output. |

On fatal errors, `CKFile::LoadFileData` always frees parsers/mapped files and restores context callbacks and automatic mode before returning.

---

## 11. Testing & Verification Plan

1. **Round-trip each class** – Save+load representative instances of every CK class (scenes, behaviors, parameters, textures, materials, meshes, manager data). Diff chunks at the byte level.
2. **Version spectrum** – Maintain fixtures for file versions 2 through 9; ensure your loader follows each legacy path.
3. **Compression coverage** – Generate files with `CKFILE_UNCOMPRESSED`, `CKFILE_CHUNKCOMPRESSED_OLD`, and `CKFILE_WHOLECOMPRESSED`. Confirm Adler-32 matches and that chunk-level decompression works.
4. **Dependency injection** – Remove/rename plugin GUIDs to exercise `CKERR_PLUGINSMISSING` while continuing to load.
5. **Dynamic object loads** – Load with `CK_LOAD_AS_DYNAMIC_OBJECT` and confirm `DeleteAllDynamicObjects` restores the original state.
6. **Reference-only flows** – Populate files with shared parameters or meshes referenced via `SaveObjectAsReference`. Ensure bit `0x800000` is honored on load.
7. **Bitmap serialization** – Verify raw/reader-based bitmap paths round-trip pixel-perfect data across multiple formats (RGBA, paletted, etc.).
8. **Stress cases** – Load files with thousands of objects to test `CheckSize` growth, chunk remapping recursion, and UI callbacks.

---

## 12. Implementation Checklist

- [ ] Provide equivalents for all CK classes used in the file layer (`CKFile`, `CKStateChunk`, `CKBufferParser`, `CKObjectManager`, `CKContext`, `CKPluginManager`).
- [ ] Mirror the bit-level layouts specified in `VIRTOOLS_FILE_FORMAT_SPEC.md` (Header1, data sections, chunk framing, bitmap payloads).
- [ ] Reproduce every load/save stage exactly (manager hooks, behavior callbacks, duplicate policies, dynamic object tracking).
- [ ] Preserve Adler-32 behavior (`CKComputeDataCRC` with initial value 0, streaming across header/data). Included files remain outside the checksum and recorded file size.
- [ ] Retain legacy code paths for versions < 6 (manager data absent) and < 7 (object descriptors absent).
- [ ] Emit and interpret plugin dependency tables; expose missing GUIDs to callers.
- [ ] Document the included-file limitation or extend the format in a backward-compatible way if extraction is required.
- [ ] Ship a regression test suite covering the scenarios listed above.

By following this guide—and cross-referencing the formal specification—you can implement a Virtools-compatible loader/saver that faithfully reproduces the original runtime’s behavior.
