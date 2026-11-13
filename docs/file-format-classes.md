# How each class stores its state

For each built-in class, the identifier sections of its state chunk, as libnmo's schemas
(`src/object/builtin/ck*_schemas.c`) read and write them. The file layout and the chunk
encoding the sections sit in are in [file-format.md](file-format.md). The Ballanced
reimplementation of the engine settled details the libnmo code leaves open; where the two
differ, the notes say so.

## Conventions

- **Inheritance.** A class's Save first writes its parent's sections (the serializer calls the parent's serializer), then its own. Each table puts the inherited part in one row ("the CKBeObject sections") and does not repeat it. CKBehavior, CKPlace and CKSpriteText skip a level of their registered parent; their tables say so.
- **Finding sections.** A reader finds a section by exact identifier value, not by bit mask. The search starts after the last section found and wraps around, so readers do not depend on section order. A section's payload runs to the next identifier.
- **Chunk and data versions.** The save path gives the chunk of a new object chunk version 7 and data version 10 (CHUNK_DEV_2_1). A loaded object that is saved again keeps the chunk version and data version of the chunk it was loaded from. The data version is one value for the whole chunk. A class that changes it does so before it calls its parent, so the inherited sections use the same value. Under each heading, "Data version: none of its own" means the class neither sets the value nor changes layout with it.
- **File mode and memory mode.** File mode is a chunk saved into a file (NMO_CHUNK_OPTION_FILE). Memory mode is any other state chunk, such as the initial-value chunks inside a CKScene. In memory mode the save flags (CK_STATESAVE_* bits) select what is written. Most classes write nothing of their own unless the flags include a bit of the class's *ONLY mask. Object ids are written as file indices in file mode and as runtime ids in memory mode.
- **Payload terms.**
  - Scalars: dword, int, float, word, string (chunk string).
  - GUID: 2 dwords. vector: 3 floats. matrix: 16 floats.
  - object id: one dword referring to an object.
  - object sequence: a count, then that many object ids.
  - sub-chunk: an embedded state chunk. sub-chunk sequence: a count, then that many sub-chunks.
  - manager sequence: manager GUID, count, then values.
  - buffer: size-prefixed bytes. raw buffer: bytes with no size prefix; each entry says how the length is known.
- **Class ids.** nmo_class_ids.h defines no class with id 7, 14, 17, 44 or 54. Ids 80 to 93 are not CKObject classes. CKAttributeManager (82) and CKMessageManager (83) have schemas for their manager data chunks; the others have none.

## Class hierarchy

Parents, as the schemas register them:

```
CKObject (1)
├── CKParameterIn (2)
├── CKParameterOperation (4)
├── CKStateObject (5)
├── CKBehaviorLink (6)
├── CKBehaviorIO (9)
├── CKSceneObject (11)
│   ├── CKBehavior (8)
│   ├── CKObjectAnimation (15)
│   ├── CKAnimation (16)
│   │   └── CKKeyedAnimation (18)
│   └── CKBeObject (19)
│       ├── CKScene (10)
│       ├── CKLevel (21)
│       ├── CKGroup (23)
│       ├── CKSound (24)
│       │   ├── CKWaveSound (25)
│       │   └── CKMidiSound (26)
│       ├── CKMaterial (30)
│       ├── CKTexture (31)
│       ├── CKMesh (32)
│       │   └── CKPatchMesh (53)
│       ├── CKRenderObject (47)
│       │   ├── CK2dEntity (27)
│       │   │   └── CKSprite (28)
│       │   │       └── CKSpriteText (29)
│       │   └── CK3dEntity (33)
│       │       ├── CKPlace (22)
│       │       ├── CKCamera (34)
│       │       │   └── CKTargetCamera (35)
│       │       ├── CKCurvePoint (36)
│       │       ├── CKSprite3D (37)
│       │       ├── CKLight (38)
│       │       │   └── CKTargetLight (39)
│       │       ├── CKCharacter (40)
│       │       ├── CK3dObject (41)
│       │       │   └── CKBodyPart (42)
│       │       ├── CKCurve (43)
│       │       └── CKGrid (50)
│       └── CKDataArray (52)
├── CKRenderContext (12)
├── CKKinematicChain (13)
├── CKSynchroObject (20)
├── CKParameter (46)
│   ├── CKParameterOut (3)
│   └── CKParameterLocal (45)
├── CKInterfaceObjectManager (48)
├── CKCriticalSectionObject (49)
└── CKLayer (51)
```

## CKObject (class id 1)

Parent: none (root of the hierarchy). Data version: none of its own (the engine's CKObject::Save sets 10).

| Identifier | Value | Contents |
|---|---|---|
| CK_STATESAVE_OBJECTHIERAHIDDEN | 0x00000018 | no data; present when the object is hidden hierarchically (CK_OBJECT_HIERACHICALHIDE) |
| CK_STATESAVE_OBJECTHIDDEN | 0x00000004 | no data; present when the object is hidden but not hierarchically (written instead of OBJECTHIERAHIDDEN) |

Notes: at most one of the two is written; a visible object writes no section, so a CKObject-only chunk can be empty. The reader checks OBJECTHIDDEN first (hidden), then OBJECTHIERAHIDDEN (hierarchically hidden), else visible. Same in file and memory mode. The object's name is not in the chunk, except in files below file version 7, where the file loader (not the schema) takes it from a CK_STATESAVE_NAME (0x00000001) section holding a string (src/session/deserializer.c:890).

## CKParameterIn (class id 2)

Parent: CKObject. Data version: set to current (10) when the chunk's is 0, unless the object came from a version-0 chunk; reads version 0 (legacy layout below), 1 to 4 (one extra object id) and 5 and later.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |
| CK_STATESAVE_PARAMETERIN_DATASHARED | 0x00000800 | if the input is shared: GUID parameter type; data version < 5: object id (unidentified, read and dropped); object id of the shared CKParameterIn |
| CK_STATESAVE_PARAMETERIN_DATASOURCE | 0x00001000 | if not shared (instead of DATASHARED): GUID parameter type; data version < 5: object id (unidentified); object id of the source CKParameter (0 when none) |
| CK_STATESAVE_PARAMETERIN_DEFAULTDATA | 0x00000400 | memory mode only, when an owner is set (instead of DATASHARED/DATASOURCE): GUID parameter type, object id owner (CKBehavior or CKParameterOperation), object id shared source (CKParameterIn, 0 when none), object id direct source (CKParameter, 0 when none) |
| CK_STATESAVE_PARAMETERIN_DISABLED | 0x00002000 | no data; present when the input is disabled |

Notes: file mode always writes DATASHARED or DATASOURCE; memory mode writes none of the three only for an object loaded from a chunk that had none of them and that has no owner or source. On read with data version >= 1, DATASHARED wins, then DATASOURCE, then the 5-dword DEFAULTDATA, where a non-zero second id makes the input shared and the third id is used only when the second is 0. Data version 0 layout: DEFAULTDATA holds only the GUID, then OWNER (0x00000080, object id), then INSHARED (0x00000100, object id of a CKParameterIn) or OUTSOURCE (0x00000200, object id of a CKParameter), then DISABLED; libnmo writes this layout back for a version-0 chunk. The old message, attribute and time type GUIDs are mapped to the current ones on read; the GUID from the file is written back.

## CKParameterOut (class id 3)

Parent: CKParameter. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKParameter sections (memory mode without CK_STATESAVE_PARAMETEROUT_VAL 0x40 in the save flags: the CKObject sections only) |
| CK_STATESAVE_PARAMETEROUT_DESTINATIONS | 0x00000020 | only when there are destinations: int count, then count x object id (destination CKParameter), written one by one rather than as an object sequence |

Notes: memory mode writes nothing after the base when the save flags are 0, and writes DESTINATIONS only when they include 0x20.

## CKParameterOperation (class id 4)

Parent: CKObject. Data version: set to current (10) when the chunk's is 0, unless the object came from the legacy file layout; file mode reads data version < 5 with one extra leading id in the NEWDATA sequence.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |
| CK_STATESAVE_OPERATIONNEWDATA | 0x00000400 | file mode: GUID operation; object sequence: data version < 5: one object id (unidentified, read and dropped), then in1 (CKParameterIn), in2 (CKParameterIn), out (CKParameterOut); count is 3 (plus the extra id) unless the trailing ends are absent |
| CK_STATESAVE_OPERATIONOP | 0x00000100 | memory mode, if the save flags include 0x100: GUID operation |
| CK_STATESAVE_OPERATIONDEFAULTDATA | 0x00000200 | memory mode, if the save flags include 0x200: object id owner (CKBehavior, 0 when none) |
| CK_STATESAVE_OPERATIONINPUTS | 0x00000040 | memory mode, if the save flags include 0x40: object id in1, sub-chunk (in1's state chunk), object id in2, sub-chunk (in2's state chunk) |
| CK_STATESAVE_OPERATIONOUTPUT | 0x00000080 | memory mode, if the save flags include 0x80: object id out, sub-chunk (out's state chunk) |

Notes: a file chunk without NEWDATA (older layout) is read from OP (GUID), DEFAULTDATA (object id owner), OUTPUT (object id out) and INPUTS (object ids in1, in2), all without sub-chunks, and libnmo writes it back in that order. Six operation GUIDs of older versions are mapped on read (table at ckparameteroperation_schemas.c:65); the GUID from the file is written back. Memory mode with save flags 0 writes only the CKObject sections. The parameter sub-chunks are kept as opaque chunks.

## CKStateObject (class id 5)

Parent: CKObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |
| CK_STATESAVE_SYNCHRODATA | 0x00000010 | int event flag |

Notes: written in file and memory mode alike. libnmo rejects a SYNCHRODATA section that is not exactly 1 dword.

## CKBehaviorLink (class id 6)

Parent: CKObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |
| CK_STATESAVE_BEHAV_LINK_NEWDATA | 0x00000020 | dword delays (activation delay in the low 16 bits, initial activation delay in the high 16 bits, each a signed 16-bit value), object id in end (CKBehaviorIO), object id out end (CKBehaviorIO) |
| CK_STATESAVE_BEHAV_LINK_CURDELAY | 0x00000004 | legacy: int activation delay |
| CK_STATESAVE_BEHAV_LINK_IOS | 0x00000008 | legacy: object id in end, object id out end (CKBehaviorIO) |
| CK_STATESAVE_BEHAV_LINK_DELAY | 0x00000010 | legacy: int initial activation delay |

Notes: new objects write NEWDATA only; the three legacy sections are read only when NEWDATA is absent, and written back only for an object loaded from them (the two layouts are never mixed; a missing delay defaults to 1). Memory mode writes the sections only when the save flags include a bit of CK_STATESAVE_BEHAV_LINKONLY (0x000000F0).

## CKBehavior (class id 8)

Parent: CKSceneObject. Data version: none of its own; the file layout branches at 5 (the layout below is 5 and later; older layouts in the notes).

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections (the serializer calls CKObject directly; CKSceneObject has no sections of its own) |
| CK_STATESAVE_BEHAVIORINTERFACE | 0x00000010 | file mode, if present: sub-chunk of editor (schematic) interface data, parsed by src/format/interface_chunk.c or kept raw; dword 0 when present but empty |
| CK_STATESAVE_BEHAVIORNEWDATA | 0x00000020 | file mode: dword behavior flags; if BUILDINGBLOCK (0x8000): GUID prototype, dword block version; if PRIORITY (0x4): int priority; if COMPATIBLECLASSID (0x10): int compatible class id; if TARGETABLE (0x40000): object id target (CKParameterIn); dword array flags; then one object sequence per set array flag: sub-behaviors (0x100, CKBehavior), sub-links (0x80000, CKBehaviorLink), operations (0x4000, CKParameterOperation), input parameters (0x200, CKParameterIn), output parameters (0x400, CKParameterOut), local parameters (0x20000, CKParameterLocal), inputs (0x800, CKBehaviorIO), outputs (0x1000, CKBehaviorIO); the first three never for a building block |
| CK_STATESAVE_BEHAVIORSUBBEHAV | 0x00000100 | memory mode, if the save flags include 0x100 and there are sub-behaviors: int count, then count x (object id CKBehavior, sub-chunk of its state) |
| CK_STATESAVE_BEHAVIORLOCALPARAMS | 0x00020000 | memory mode, if the save flags include 0x20000 and the behavior is not a building block: int count, then count x (object id CKParameterLocal, sub-chunk of its state); a single dword 0 when there are none |
| CK_STATESAVE_BEHAVIORSINGLEACTIVITY | 0x00200000 | file mode, if present: dword scene activity flags |

Notes: the written behavior flags clear LOCKED (0x20000000) unless BUILDINGBLOCK, set PRIORITY iff priority != 0, set COMPATIBLECLASSID iff the class id is not 19 (CKBeObject), and clear LAUNCHEDONCE (0x80000000); the array flags always match the non-empty arrays, other bits of a loaded value are kept. Data version < 5 (file): NEWDATA holds GUID prototype, dword flags, dword compatible class id, dword behavior type (1 = script), int priority, object id owner (CKBeObject), dword block version (0 for a graph); each array follows in its own section as an object sequence, written only when non-empty, in the order SUBBEHAV, SUBLINKS (0x00080000), OPERATIONS (0x00004000) (graphs only), INPARAMS (0x00000200), LOCALPARAMS, OUTPARAMS (0x00000400), INPUTS (0x00000800), OUTPUTS (0x00001000), then SINGLEACTIVITY. Older chunks with no NEWDATA are read from PROTOGUID (0x00040000, GUID), FLAGS (0x00000040, int; USEFUNCTION 0x8 marks a building block), COMPATIBLECID (0x00000080, dword), TYPE (0x00008000, dword), OWNER (0x00010000, object id), PRIORITY (0x00800000, int) and TARGET (0x01000000, object id), and written back with the version < 5 NEWDATA. A file chunk of data version >= 5 without NEWDATA is rejected. INTERFACE, NEWDATA and SINGLEACTIVITY may instead carry 0x00000001, 0x00000002 and 0x00000004 (local *_LEGACY defines); the reader falls back to them and the writer keeps them. A memory chunk that holds NEWDATA is read with the file layout.

## CKBehaviorIO (class id 9)

Parent: CKObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |
| CK_STATESAVE_BEHAV_IOFLAGS | 0x00000008 | dword I/O flags: 0x1 input, 0x2 output, 0x100 active |

Notes: written whenever present (always for new objects); memory mode writes it only when the save flags include a bit of CK_STATESAVE_BEHAVIOONLY (0x000000F0).

## CKScene (class id 10)

Parent: CKBeObject. Data version: set to current (10) when the output chunk has none; reads data version >= 1 (0 is rejected), with the old per-object flag encoding below 8.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKBeObject sections |
| CK_STATESAVE_SCENENEWDATA | 0x00020000 | object id: owning CKLevel; int count N; if N > 0: object sequence of N CKSceneObject ids, then sub-chunk sequence of 2N sub-chunks (per object: initial-value sub-chunk, reserved sub-chunk), then N x dword scene-object flags |
| CK_STATESAVE_SCENELAUNCHED | 0x00040000 | dword environment settings (CK_SCENE_FLAGS) |
| CK_STATESAVE_SCENERENDERSETTINGS | 0x00080000 | dword background color (packed), dword ambient light color (packed), dword fog mode (VXFOG_MODE), dword fog color (packed), float fog start, float fog end, float fog density, object id background CKTexture, object id starting CKCamera |

Notes: the per-object record is spread over three parallel arrays: object i is sequence id i, sub-chunks 2i and 2i+1, and flags dword i. The initial-value sub-chunk is the object's state saved in memory mode with CK_STATESAVE_ALL (may be null). The engine writes the reserved sub-chunk as null; libnmo writes back whatever it read. Flags at data version >= 8: CK_SCENEOBJECT_* bits (0x01 START_ACTIVATE, 0x08 ACTIVE, 0x10 START_DEACTIVATE, 0x40 START_RESET). Below 8: 0x01 = start activate (clear = start deactivate), 0x02 = start reset, 0x08 = active. A write at data version < 8 fails for flags this old encoding cannot hold (ckscene_schemas.c:438). File mode and memory mode use the same layout. The reader requires SCENENEWDATA to end exactly after the flags, SCENELAUNCHED to be 1 dword and SCENERENDERSETTINGS 9 dwords.

## CKSceneObject (class id 11)

Parent: CKObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |

Notes: no sections of its own in either mode; scene membership is rebuilt from the CKScene chunks.

## CKRenderContext (class id 12)

Parent: CKObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |

Notes: no sections of its own in either mode. Ballanced has no Save/Load override for CKRenderContext either.

## CKKinematicChain (class id 13)

Parent: CKObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |
| CK_STATESAVE_KINEMATICCHAINALL | 0x000000FF | object id (reserved; the engine writes a null object), object id start effector (CKBodyPart), object id end effector (CKBodyPart) |

Notes: memory mode writes nothing past the CKObject sections unless the save flags include a CK_STATESAVE_KINEMATICCHAINALL bit. The reader accepts a section longer than 3 dwords and ignores the extra dwords. libnmo writes the section only when the object's state holds it (it was in the loaded chunk); the engine writes it on every file save. Unclear: what the reserved first object id was meant for.

## CKObjectAnimation (class id 15)

Parent: CKSceneObject. Data version: the SHARED, CONTROLLERS and NEWDATA layouts need data version >= 1 (set to 10 when the chunk has 0); data version 0 holds the legacy layout, which is read and written back only at data version 0 (libnmo refuses to write it at a higher version).

| Identifier | Value | Contents |
|---|---|---|
| | | the CKSceneObject sections |
| CK_STATESAVE_OBJANIMSHARED | 0x02000000 | data version >= 1, keys owned by another animation: object id owner (CKObjectAnimation), vector root position, 4 floats (the engine writes 0 and ignores them on read), dword flags, object id entity (CK3dEntity); if flags & 0x80 (merged): float merge factor, object id anim1, object id anim2 (CKObjectAnimation) |
| CK_STATESAVE_OBJANIMCONTROLLERS | 0x04000000 | data version >= 1, own keys (instead of SHARED): vector root position, 4 floats (as in SHARED), dword flags, object id entity (CK3dEntity), float length; if flags & 0x80: float merge factor, object id anim1, object id anim2; then per controller: dword controller type, dword blob size in dwords, blob (layouts in the notes); a dword 0 ends the list |
| CK_STATESAVE_OBJANIMNEWDATA | 0x00001000 | data version >= 1, older layout (instead of SHARED/CONTROLLERS; read, and written back only for objects loaded in it): vector root position, 4 floats, int morph vertex count, int morph key count, dword flags, object id entity (CK3dEntity), float length; if flags & 0x80: float merge factor, object id anim1, object id anim2; then morph key count x (float time, dword byte size, raw buffer of that size: vertex positions, 12 bytes each); then 4 old key arrays for position, scale, rotation, scale axis, each dword byte size, int key count, raw buffer of byte size |
| CK_STATESAVE_OBJANIMMORPHCOMP | 0x01000000 | NEWDATA layout with morph keys only: per morph key, dword byte size, raw buffer (compressed normals, 4 bytes per vertex) |
| CK_STATESAVE_OBJANIMMORPHNORMALS | 0x00800000 | NEWDATA layout with morph keys only: per morph key, dword byte size, raw buffer (older normal encoding, 4 bytes per vertex; the engine converts it on load) |
| CK_STATESAVE_OBJANIMMORPHKEYS | 0x00010000 | data version 0: payload not decoded (the engine seeks it and reads nothing); libnmo keeps the dwords and writes them back |
| CK_STATESAVE_OBJANIMMORPHKEYS2 | 0x00200000 | data version 0: int morph key count; if > 0: int vertex count, then count x (float time, dword byte size, raw buffer: vertex positions, 12 bytes each) |
| CK_STATESAVE_OBJANIMPOSKEYS | 0x00004000 | data version 0: old key array of position keys (dword byte size, int key count, raw buffer) |
| CK_STATESAVE_OBJANIMROTKEYS | 0x00008000 | data version 0: old key array of rotation keys, then old key array of scale-axis keys |
| CK_STATESAVE_OBJANIMSCLKEYS | 0x00020000 | data version 0: old key array of scale keys |
| CK_STATESAVE_OBJANIMFLAGS | 0x00040000 | data version 0: dword flags |
| CK_STATESAVE_OBJANIMENTITY | 0x00080000 | data version 0: object id entity (CK3dEntity) |
| CK_STATESAVE_OBJANIMLENGTH | 0x00002000 | data version 0: float length |
| CK_STATESAVE_OBJANIMMERGE | 0x00100000 | data version 0: float merge factor, int merged (sets or clears flag 0x80), object id anim1, object id anim2 (CKObjectAnimation) |
| CK_STATESAVE_OBJANIMNEWDATA | 0x00001000 | data version 0: vector root position |

Notes: memory mode writes nothing past the CKSceneObject sections unless the save flags include a CK_STATESAVE_OBJANIMALL bit (0x07FFFFFF). A data version >= 1 chunk holds exactly one of SHARED, CONTROLLERS, NEWDATA (the reader tries them in that order). The engine only writes SHARED or CONTROLLERS, and in file mode writes SHARED only when the owner is already saved in the file; libnmo writes a SHARED state with no owner as CONTROLLERS with no controllers and length 100. Extra dwords at the end of SHARED are kept and written back. A data version >= 1 chunk with none of the three sections: libnmo keeps its other identifier sections and writes them back after the CKSceneObject sections.

CONTROLLERS blobs (the engine writes position, rotation, scale, scale axis, morph in that order; libnmo keeps the loaded order): every non-morph blob is int key count, then the keys. Linear position 0x637C4301 and linear scale 0x654A3A04: 16-byte keys (float time, vector). Linear rotation 0x49ED4002 and linear scale axis 0x2F200B08: 20-byte keys (float time, quaternion as 4 floats). TCB position 0x347E4A01 and TCB scale 0x1B545904: 36-byte keys (float time, vector, floats tension, continuity, bias, ease to, ease from). TCB rotation 0x45B52A02 and TCB scale axis 0x32595908: 40-byte keys (float time, quaternion, the same 5 floats). Bezier position 0x921AB801 and Bezier scale 0x18AB4404: variable-size keys: float time, vector, dword holding two 16-bit flag words, then one 12-byte tangent vector for each flag word that has bit 0x20. Morph 0x73847810: int key count, int vertex count, dword normals flag, then key count x (float time, vertex count x vector, and if the normals flag is set vertex count x 4-byte compressed normal). An empty controller is written with blob size 1 and a zero key count (morph: size 3, three zero dwords). The reader also maps the type codes 1, 2, 4 and 0x10 to linear position, linear rotation, linear scale and morph; unclear whether any engine version writes these codes.

Old key arrays (NEWDATA and data version 0): position and scale keys are 16 bytes (float time, vector), rotation keys 20 bytes (float time, quaternion), scale-axis keys 24 bytes (float time, unused float, quaternion). The byte size may be less than key count x key size; libnmo fills the missing bytes with zeros and quaternion w = 1, and writes the full size back. Unclear: the exact MORPHNORMALS per-vertex encoding (the engine reads it into 8-byte two-dword records but sizes it as 4 bytes per vertex; libnmo keeps the bytes undecoded).

## CKAnimation (class id 16)

Parent: CKSceneObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKSceneObject sections |
| CK_STATESAVE_ANIMATIONDATA | 0x00000010 | dword flags, float frame rate; older 3-dword form (read, and written back for objects loaded from it): int can be interrupted (flag 0x4), int linked to frame rate (flag 0x1), float frame rate |
| CK_STATESAVE_ANIMATIONLENGTH | 0x00000040 | float length |
| CK_STATESAVE_ANIMATIONBODYPARTS | 0x00000080 | object sequence of CKBodyPart (legacy; the engine writes an empty one and drops it on read), then object id root entity (CK3dEntity) |
| CK_STATESAVE_ANIMATIONCHARACTER | 0x00000100 | object id character (CKCharacter) |
| CK_STATESAVE_ANIMATIONCURRENTSTEP | 0x00000200 | float current step |

Notes: file mode writes every section; memory mode writes only the sections whose identifier bit is in the save flags. The reader takes ANIMATIONDATA only when it is exactly 2 dwords (current form) or 3 dwords (older form) and ignores it otherwise. Chunk version < 4: the body part array uses the old array layout (dword lead, 0 = empty; 4 skipped dwords; int count; count raw object ids).

## CKKeyedAnimation (class id 18)

Parent: CKAnimation. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKAnimation sections |
| CK_STATESAVE_KEYEDANIMANIMLIST | 0x00001000 | object sequence of CKObjectAnimation (the animations it drives) |
| CK_STATESAVE_KEYEDANIMMERGE | 0x00100000 | int merged, float merge factor |
| CK_STATESAVE_KEYEDANIMSUBANIMS | 0x00200000 | memory mode only: dword count, then count x (object id CKObjectAnimation, sub-chunk: that animation's state chunk) |

Notes: file mode writes ANIMLIST and MERGE, never SUBANIMS; memory mode writes each of the three only when its identifier bit is in the save flags. The reader ignores SUBANIMS in file mode. Chunk version < 4: ANIMLIST uses the old array layout (see CKAnimation).

## CKBeObject (class id 19)

Parent: CKSceneObject. Data version: set to current (10) when the chunk has 0 and a modern DATAS section is written; reads every version, with a legacy layout below 5.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKSceneObject sections |
| CK_STATESAVE_SCRIPTS | 0x00000800 | file mode only: object sequence of the CKBehavior scripts; written when the object has scripts or its chunk had the section |
| CK_STATESAVE_DATAS | 0x00000040 | file mode: dword flags (0x10000000 = priority follows), then int priority if that bit is set; written when the chunk had it or the priority is nonzero. Memory mode: a single int, written only for an object loaded from a memory chunk that had it |
| CK_STATESAVE_ATTRIBUTES | 0x00000010 | obsolete, written back only for an object loaded from it (instead of NEWATTRIBUTES): int count, then count x (int compatible class id (absent in the oldest form), string name, string category, GUID parameter type, object id of the CKParameterOut) |
| CK_STATESAVE_NEWATTRIBUTES | 0x00000011 | object sequence of the attribute parameters (CKParameterOut); memory mode only: sub-chunk sequence, one state chunk per attribute parameter; then manager sequence (CKAttributeManager GUID 0x3D242466, 0x00000000; count = attribute count) of dword attribute types |
| CK_STATESAVE_SINGLEACTIVITY | 0x00000400 | file mode only, when the chunk had it: dword flags |

Notes: memory mode writes nothing past the CKObject sections unless the save flags include a CK_STATESAVE_BEOBJECTONLY bit (0x00000FF0). Data version < 5 (file): scripts may sit under CK_STATESAVE_BEHAVIORS (0x00000100, same object sequence; written back only below 5), and DATAS holds dword flags, 3 dwords (kept as is), int priority. The attribute types in NEWATTRIBUTES index the CKAttributeManager table saved in the same file. The oldest ATTRIBUTES form (no class id per entry) is detected from the first two values (class id outside 0..0x35 or name length outside 1..0x41).

## CKSynchroObject (class id 20)

Parent: CKObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |
| CK_STATESAVE_SYNCHRODATA | 0x00000010 | int max waiters, object sequence of arrived objects (CKBeObject), object sequence of passed objects (CKBeObject) |

Notes: written in file and memory mode alike. libnmo rejects a SYNCHRODATA section shorter than 3 dwords or with dwords left after the second sequence.

## CKLevel (class id 21)

Parent: CKBeObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKBeObject sections |
| CK_STATESAVE_LEVELDEFAULTDATA | 0x20000000 | object sequence (legacy CKObjectArray, written empty by the engine), object sequence (legacy XObjectPointerArray, written empty by the engine), object sequence of CKScene ids (scene list) (file mode only) |
| CK_STATESAVE_LEVELSCENE | 0x80000000 | object id current CKScene, object id level CKScene, sub-chunk: the level scene's CKScene chunk saved in file mode (may be null) (file mode only) |
| CK_STATESAVE_LEVELINACTIVEMAN | 0x00002000 | GUIDs of inactive managers, no count: count = section size in bytes / 8 (file mode only, optional) |
| CK_STATESAVE_LEVELDUPLICATEMAN | 0x00004000 | manager-name strings ending in a null string, then raw buffer up to the end of the section (file mode only, written only after LEVELINACTIVEMAN) |

Notes: memory mode writes and reads nothing past the CKBeObject sections. The engine writes LEVELINACTIVEMAN and LEVELDUPLICATEMAN only when it has inactive managers. It writes a null string after every manager it walks (Ballanced CKLevel.cpp:429-435), so readers stop at the first null. libnmo keeps the dwords after that null up to the section end as a raw tail and writes them back unchanged (cklevel_schemas.c:646, 817). The object ids inside the level-scene sub-chunk are file indices. libnmo finds them by re-serializing the scene, then rewrites them as indices of the file being written; ids it cannot map become -1 (cklevel_schemas.c:274, 346, 364). Unclear: what older writers put in the two legacy object sequences; libnmo only round-trips them.

## CKPlace (class id 22)

Parent: CK3dEntity. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKBeObject sections (CK3dEntity's own sections are not written) |
| CK_STATESAVE_PLACECAMERA | 0x00002000 | object id: CKCamera |
| CK_STATESAVE_PLACELEVEL | 0x00008000 | object id: CKLevel (file mode only) |
| CK_STATESAVE_PLACEPORTALS | 0x00001000 | int count, then count x (object id CKPlace, object id CK3dEntity portal) |
| CK_STATESAVE_PLACEREFERENCES | 0x00004000 | object sequence of CK3dEntity ids |

Notes: Save and Load both skip CK3dEntity and call CKBeObject directly, as the engine does. Each section is written only when the place has that data. When save flags are non-zero, only the sections they select are written (CK_STATESAVE_PLACE* bits). File mode with save flags 0 writes every present section. Memory mode with save flags 0 writes only the CKBeObject sections. The reader parses these sections only in file mode, so a memory-mode chunk is read as CKBeObject only. The engine's Save never writes PLACEREFERENCES. Its Load reads that section in file mode and adds the listed entities as children (Ballanced CKPlace.cpp:316). libnmo writes it back when the loaded chunk had it.

## CKGroup (class id 23)

Parent: CKBeObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKBeObject sections |
| CK_STATESAVE_GROUPALL | 0x000FFFFF | int count, then count x object id (CKBeObject members) |

Notes: the identifier is GROUPALL, not GROUPDATA. File mode writes this section whenever the group has members or was loaded with one. Memory mode writes it only when the save flags include a GROUPALL bit. The reader looks for the section in both modes. libnmo writes count + ids with each id tracked on its own (ckgroup_schemas.c:262). The engine writes the same dwords as an object sequence (XObjectPointerArray::Save), so the payload matches and only the chunk's id-position list differs.

## CKSound (class id 24)

Parent: CKBeObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKBeObject sections |
| CK_STATESAVE_SOUNDFILENAME | 0x00001000 | dword save options (CK_SOUND_SAVEOPTIONS: 0 external, 1 include original file, 2 use global), string sound file name without its directory |

Notes: written in file and memory mode alike. The sound data is never in the chunk; with option 1 (directly or through the global option) the engine adds the file to the file's included files. The reader skips any dwords left at the end of the section.

## CKWaveSound (class id 25)

Parent: CKSound. Data version: set to 10 when the chunk has none; DATA2 has three layouts (data version < 2, = 2, >= 3), all read and written.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKSound sections |
| CK_STATESAVE_WAVSOUNDFILE | 0x00100000 | string wave file name (file mode: only when the loaded chunk had it) |
| CK_STATESAVE_WAVSOUNDDURATION | 0x00800000 | int duration |
| CK_STATESAVE_WAVSOUNDDATA2 | 0x00400000 | data version >= 3 (24 dwords): dword state flags (CK_WAVESOUND_*), float priority, float gain, float pan, float pitch, 3 dwords reserved, float cone inside angle, float cone outside angle, float cone outside gain, float min distance, float max distance, dword distance behavior, object id of the attached object (CK3dEntity), buffer position (12 bytes, vector), buffer direction (12 bytes, vector), dword reserved |

Notes: data version 2: dword state flags, float priority, 2 dwords reserved, float gain, float pan, float pitch, dword reserved; then, unless the type bits (state & 0x7) are 1 (background), 2 dwords reserved, the cone, distance, attached object, position and direction fields as above, dword reserved (8 or 26 dwords). Data version < 2: 20 dwords, word 8 = loop flag (sets CK_WAVESOUND_LOOPED 0x8). Data version 10 changes no layout: in the >= 3 layout the engine reads the cone floats but applies them only from data version 10 on (the version 2 layout always applies them); libnmo keeps them at any version.
File mode writes each section the object holds (loaded or set); memory mode writes nothing past the CKSound sections unless the save flags include a CK_STATESAVE_WAVSOUNDONLY bit (0x0FF00000), then each section whose own flag is set. The engine's Save never writes WAVSOUNDFILE; its Load reads it.

## CKMidiSound (class id 26)

Parent: CKSound. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKSound sections |
| CK_STATESAVE_MIDISOUNDFILE | 0x00100000 | string MIDI file name (only when the loaded chunk had it) |

Notes: the engine's Save writes no section of its own; its Load takes the MIDI file name from MIDISOUNDFILE when present, else from the CKSound file name. libnmo writes MIDISOUNDFILE back only to keep one it read, in either mode.

## CK2dEntity (class id 27)

Parent: CKRenderObject. Data version: set to 10 when the chunk has none (unless the object came from a legacy chunk); layout branches at data version 5, with a legacy layout below 5 that is still read and re-written.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKRenderObject sections |
| CK_STATESAVE_2DENTITYONLY | 0x0010F000 | dword flags (CK_2DENTITY_FLAGS; the writer sets 0x00010000 / 0x00020000 / 0x00040000 when the matching block follows), 4 floats rect (left, top, right, bottom: the homogeneous rect if flag 0x00000200 USEHOMOGENEOUSCOORD, else the screen rect); if 0x00010000: 4 floats source rect; if 0x00020000: int z-order; if 0x00040000: object id of the parent (CK2dEntity) |
| CK_STATESAVE_2DENTITYMATERIAL | 0x00200000 | object id of the material (CKMaterial), only when one is set (data version >= 5) |

Notes: the reader keeps flags & 0xFFF8F7FF (drops 0x800 UPDATEHOMOGENEOUSCOORD and the three block bits); without the source-rect block the source rect is (0,0,1,1) for CK2dEntity and (0,0,0,0) for subclasses; without 2DENTITYONLY the constructor state stays (flags 0x00280501). MATERIAL is read only from class-id-27 chunks (0x00200000 is also CKSprite's movie-name identifier). The writer emits the source rect when the object holds one and the z-order when nonzero or loaded.
Data version < 5: separate sections, no parent, no material: CK_STATESAVE_2DENTITYFLAGS 0x00004000 (dword flags), CK_STATESAVE_2DENTITYPOS 0x00008000 (int x, int y; 2 floats if USEHOMOGENEOUSCOORD), CK_STATESAVE_2DENTITYSIZE 0x00002000 (int width, int height; 2 floats if USEHOMOGENEOUSCOORD), CK_STATESAVE_2DENTITYSRCSIZE 0x00001000 (4 ints, assigned in order to source right, left, top, bottom), CK_STATESAVE_2DENTITYZORDER 0x00100000 (int). On load the flags get 0x1 forced on (the entity is hidden if it was clear), 0x800 cleared, STICKLEFT|STICKTOP (0x00280000) set.

## CKSprite (class id 28)

Parent: CK2dEntity. Data version: none of its own (CK2dEntity sets the chunk's); see notes for version 7.

| Identifier | Value | Contents |
|---|---|---|
| | | the CK2dEntity sections |
| CK_STATESAVE_SPRITESHARED | 0x00080000 | object id of the CKSprite whose bitmap this one shares (only when set; memory mode: only with save flag SPRITESHARED) |
| CK_STATESAVE_SPRITEREADER | 0x10000000 | file mode, no shared sprite, slots stored through a bitmap reader: int slot count, int width, int height, int bpp, then per slot: dword type (0 = empty slot, nothing follows); else dword file extension (4 chars), GUID reader, buffer encoded image; if type 2: int distinct alpha count, then int alpha value if the count is 1, else buffer alpha plane (width x height bytes) |
| CK_STATESAVE_SPRITECOMPRESSED | 0x00800000 | file mode, no shared sprite, in place of SPRITEREADER, slots as raw planes: int slot count, then per slot: int bpp (0 = empty slot, nothing follows), int width, int height, dword alpha mask, dword red mask, dword green mask, dword blue mask, dword compression (low 4 bits: 0 = planes as stored, 1 = DCT-coded planes), buffer blue plane, buffer green plane, buffer red plane, buffer alpha plane |
| CK_STATESAVE_SPRITEBITMAPS | 0x00040000 | file mode, no shared sprite, in place of the two above (obsolete): int slot count, then per slot: int (unused), buffer image |
| CK_STATESAVE_SPRITEFILENAMES | 0x00400000 | file mode, no shared sprite: int slot count, then count x string slot file name |
| CK_STATESAVE_SPRITEAVIFILENAME | 0x00200000 | file mode, no shared sprite: string movie file name |
| CK_STATESAVE_SPRITEVIDEOFORMAT | 0x40000000 | file mode, only when the loaded chunk had it: dword video format |
| CK_STATESAVE_SPRITETRANSPARENT | 0x00020000 | dword transparent color (packed), dword transparent flag (0/1) |
| CK_STATESAVE_SPRITECURRENTIMAGE | 0x00010000 | int current slot |
| CK_STATESAVE_SPRITEFORMAT | 0x20000000 | file mode only: dword save options (CK_BITMAP_SAVEOPTIONS: 0 raw data, 1 external, 2 image format, 3 use global, 4 include original file), buffer CKBitmapProperties blob (size 0 when none) |

Notes: the five bitmap sections are the CKBitmapData slots (same helper and slot layout as CKTexture, with these identifiers); at most one of READER / COMPRESSED / BITMAPS is present. The engine writes only AVIFILENAME when a movie is set, else READER (option 2 with a reader) or COMPRESSED, then FILENAMES always; on read the FILENAMES count resets the slot count. The engine applies the FORMAT blob only for data version > 6; libnmo keeps it at any version. File mode writes TRANSPARENT, CURRENTIMAGE and FORMAT when the object holds them; memory mode writes only TRANSPARENT and CURRENTIMAGE, each when its save flag is set, and reads TRANSPARENT, CURRENTIMAGE, SPRITESHARED.
The engine's Save never writes SPRITESHARED, and its file-mode Load reads nothing else of CKSprite's when SPRITESHARED is present; libnmo's file-mode reader still reads VIDEOFORMAT, TRANSPARENT, CURRENTIMAGE and FORMAT. The engine neither reads nor writes VIDEOFORMAT (later engines write one dword).

## CKSpriteText (class id 29)

Parent: CKSprite. Data version: none of its own (CK2dEntity sets the chunk's).

| Identifier | Value | Contents |
|---|---|---|
| | | the CK2dEntity sections (not CKSprite's) |
| CK_STATESAVE_SPRITETEXT | 0x01000000 | string text ("" when none) |
| CK_STATESAVE_SPRITEFONT | 0x02000000 | string font name, int size, int weight, int italic, int underline |
| CK_STATESAVE_SPRITETEXTCOLOR | 0x04000000 | dword font color (packed), dword background color (packed) |

Notes: written in file and memory mode alike. Save skips CKSprite and calls CK2dEntity directly (the bitmap is rebuilt by Redraw); Load goes through CKSprite's reader first, so CKSprite sections are read if present. Ballanced notes that the original Load's nested ReadInt calls read the four font ints in reverse order; libnmo reads them in write order.

## CKMaterial (class id 30)

Parent: CKBeObject. Data version: set to current (10) when 0, unless a legacy layout is being kept; reads every version, with a legacy MATDATA layout below 5 and value fix-ups below 4.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKBeObject sections |
| CK_STATESAVE_MATDATA | 0x00001000 | dword diffuse, dword ambient, dword specular, dword emissive (packed ARGB colors), float specular power, object id (CKTexture, texture 0), dword texture border color (packed ARGB), dword modes (bits 0-3 texture blend, 4-7 min filter, 8-11 mag filter, 12-15 source blend, 16-19 dest blend, 20-23 shade mode, 24-27 fill mode, 28-31 texture address), dword flags (bits 0-7 material flags, 8-11 Z compare func, 16-19 alpha compare func, 24-31 alpha ref; bits 12-15 and 20-23 zero) |
| CK_STATESAVE_MATDATA5 | 0x00010000 | object id (CKParameter, effect parameter), dword VX_EFFECT; only if an effect is set and it has a parameter |
| CK_STATESAVE_MATDATA3 | 0x00004000 | dword VX_EFFECT; only if an effect is set without a parameter (written instead of MATDATA5) |
| CK_STATESAVE_MATDATA2 | 0x00002000 | 3 object ids (CKTexture): textures 1 to 3; only if any of them is set |

Notes: memory mode writes only the CKBeObject sections unless the save flags include CK_STATESAVE_MATERIALONLY (0x000FF000) bits. In file mode libnmo omits MATDATA when the source chunk had none and every value is the constructor default (the engine always writes it). Data version < 5 (read; written back only in file mode, for a chunk loaded at < 5): MATDATA holds 4 colors (diffuse, ambient, specular, emissive), each a dword byte size (16) followed by that many bytes of RGBA floats, then float specular power, object id (CKTexture), dword flags (low byte), 8 dwords (texture blend, min filter, mag filter, source blend, dest blend, shade mode, fill mode, texture address), dword texture border color, dword Z compare func (0 read as VXCMP_LESSEQUAL); it has no alpha func or alpha ref (read as VXCMP_ALWAYS and 0). Below 4 the reader also maps a flags low byte of 1 to 7 and 0 to 6, turns ONE/ZERO blending into SRCALPHA/INVSRCALPHA when the diffuse alpha is below 1, and sets flag 0x08 and clears 0x02 when the dest blend is not ZERO. Readers apply MATDATA3 and then MATDATA5 (the last one wins); a shade mode above GOURAUD is clamped to GOURAUD and an alpha func of 0 becomes ALWAYS.

## CKTexture (class id 31)

Parent: CKBeObject. Data version: set to current (10) when 0, unless a legacy layout is being kept; reads every version, with a legacy layout below 5.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKBeObject sections |
| CK_STATESAVE_TEXREADER | 0x00100000 | bitmap (CKBitmapData slots, reader form): int slot count, int width, int height, int bpp, then per slot: dword format type (0 = empty slot, nothing follows), 4 bytes file extension, GUID bitmap reader, buffer encoded image; format type 2 adds int distinct alpha count, then int alpha value if the count is 1, else buffer alpha plane (width x height bytes) |
| CK_STATESAVE_TEXCOMPRESSED | 0x00020000 | bitmap (CKBitmapData slots, raw form): int slot count, then per slot: int bpp (0 = empty slot, nothing follows), int width, int height, dword alpha mask, dword red mask, dword green mask, dword blue mask, dword compression (low 4 bits: 0 = planes as stored, 1 = DCT-coded planes), buffer blue plane, buffer green plane, buffer red plane, buffer alpha plane (width x height bytes each, bottom row first; the alpha buffer is empty without an alpha mask) |
| CK_STATESAVE_TEXBITMAPS | 0x00004000 | obsolete bitmap form, written back only when the source had it: int slot count, then per slot: int (unused), buffer image file (optionally prefixed by a 5-char tag CKTGA, CKJPG, CKBMP, CKDIB, CKTIF, CKGIF or CKPCX) |
| CK_STATESAVE_TEXFILENAMES | 0x00010000 | int count, then count x string slot file name |
| CK_STATESAVE_TEXAVIFILENAME | 0x00001000 | string movie file name |
| CK_STATESAVE_PICKTHRESHOLD | 0x00200000 | int pick threshold; only if set; read only at data version >= 5 |
| CK_STATESAVE_OLDTEXONLY | 0x002FF000 | dword packed state (bits 0-7 mipmap level, 0xFF = -1; 0x100 transparent; 0x200 desired video format follows; 0x400 cube map; bits 16-23 CK_TEXTURE_SAVEOPTIONS), then dword transparent color (packed ARGB), int current slot (if slot count > 1), dword VX_PIXELFORMAT desired video format (if 0x200) |
| CK_STATESAVE_TEXSAVEFORMAT | 0x00080000 | buffer CKBitmapProperties (the texture's own save format); only if one is set |
| CK_STATESAVE_USERMIPMAP | 0x00400000 | int count, then count x raw bitmap slot (same record as in TEXCOMPRESSED); only if user mipmaps exist |

Notes: only one of TEXREADER, TEXCOMPRESSED and TEXBITMAPS is present (read in that priority); the engine writes either TEXAVIFILENAME alone or TEXREADER/TEXCOMPRESSED followed by TEXFILENAMES, and libnmo writes back whichever bitmap sections were loaded (helper nmo_bitmap_slots_write, shared with CKSprite under other identifiers). The TEXFILENAMES count resets the slot count on read. Memory mode writes only the CKBeObject sections unless the save flags include CK_STATESAVE_OLDTEXONLY bits. OLDTEXONLY's trailing fields are told apart by size: 3 dwords = color, slot, format; with 2 or 1 dwords the reader picks them from the slot count and bit 0x200 (the engine always writes the color, libnmo only when the state has one); when OLDTEXONLY is absent libnmo also accepts the identifier CK_STATESAVE_TEXONLY (0x00FFF000) and keeps it on write. Data version < 5 (written back only in file mode): after the bitmap sections come TEXTRANSPARENT (0x00008000: dword transparent color, dword transparent bool), TEXCURRENTIMAGE (0x00002000: int current slot), TEXVIDEOFORMAT (0x00040000: int use mipmap, then optional raw bytes to the end of the section: dword size and a VxImageDescEx tail from Width on, whose masks give the desired video format) and TEXSAVEFORMAT (dword save options, buffer CKBitmapProperties), instead of PICKTHRESHOLD, OLDTEXONLY, TEXSAVEFORMAT and USERMIPMAP.

## CKMesh (class id 32)

Parent: CKBeObject. Data version: set to current (10) when 0 or below 9, so CKMesh always writes the current layout; reads older versions with legacy vertex, face and line layouts below 9 (branches at 5 and 1).

| Identifier | Value | Contents |
|---|---|---|
| | | the CKBeObject sections |
| CK_STATESAVE_MESHFLAGS | 0x00002000 | dword VXMESH_FLAGS |
| CK_STATESAVE_MESHMATERIALS | 0x00100000 | int count, then count x (object id (CKMaterial), int reserved 0); only if count > 0 |
| CK_STATESAVE_MESHFACES | 0x00010000 | int face count, then face count x (dword index0 \| index1 << 16, dword index2 \| material group index << 16); only if faces |
| CK_STATESAVE_MESHLINES | 0x00040000 | int line count, then buffer of line count x 2 words (vertex indices); only if lines |
| CK_STATESAVE_MESHVERTICES | 0x00020000 | int vertex count, dword save flags (0x01 one diffuse color, 0x02 one specular color, 0x04 no normals, 0x08 one UV, 0x10 no positions), then a raw block whose first dword is its length in dwords (itself included): positions (count x vector) unless 0x10, diffuse colors (1 dword if 0x01, else count dwords, packed ARGB), specular colors (likewise with 0x02), normals (count x vector) unless 0x04, UVs (1 x (float u, float v) if 0x08, else count x); only if vertices |
| CK_STATESAVE_MESHCHANNELS | 0x00004000 | int count, then count x (object id (CKMaterial), dword channel flags, dword source blend, dword dest blend, int uv count, uv count x (float u, float v)); only if channels exist or the source chunk had the section |
| CK_STATESAVE_MESHWEIGHTS | 0x00080000 | int count; if the weights are not all equal, buffer of count floats; then float (the first weight, which every vertex takes when there is no buffer); only if weights |
| CK_STATESAVE_MESHFACECHANMASK | 0x00008000 | int face count, then face count / 2 x dword (mask[2i] \| mask[2i+1] << 16), then word last mask if the count is odd; only if channels exist and some face mask is not 0xFFFF |
| CK_STATESAVE_PROGRESSIVEMESH | 0x00800000 | int vertex count, int morph enabled, int morph step, raw buffer of progressive-mesh data (length: rest of the section); only if progressive-mesh data exists |

Notes: memory mode writes only the CKBeObject sections unless the save flags include CK_STATESAVE_MESHONLY (0x00FFF000) bits. libnmo sets vertex save flags 0x01/0x02/0x08 when every value equals the first, 0x04 when all normals are zero (unless the source stored zero normals), 0x10 when VXMESH_PROCEDURALPOS is set and all positions are zero. MESHWEIGHTS readers tell the two forms apart by section size (more than 2 dwords = buffered). Data version < 9 (read only): MESHMATERIALS is ignored and the groups come from the face materials; MESHVERTICES is int count, then at >= 5 dword save flags and arrays without the length dword: positions unless 0x10, then for a lit mesh (VXMESH_PRELITMODE clear in MESHFLAGS) normals unless 0x04, for a prelit mesh diffuse colors (1 or count, per 0x01) and specular colors (per 0x02), then UVs (per 0x08); at 1 to 4 count x (vector position, lit: vector normal / prelit: dword color, dword specular, then float u, float v); at 0 count x (dword 12, vector position, dword 12, vector normal, dword color, dword specular, float u, float v). MESHFACES is int face count, then at >= 1 int group count, group count x (object id (CKMaterial), int n, n x (dword index0 \| index1 << 16, dword index2 in the low word)), at 0 face count x (int a, int b, int c, dword unused, object id (CKMaterial)). MESHLINES at 0 is int count, count x (int a, int b).

## CK3dEntity (class id 33)

Parent: CKRenderObject. Data version: set to current (10) when the chunk has 0 and its class id is 33; reads every version, the skin layout branching at 6.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKRenderObject sections |
| CK_STATESAVE_MESHS | 0x00004000 | object id of the current mesh (CKMesh), then object sequence of the entity's meshes (CKMesh); never in a chunk of class id 43 (CKCurve) |
| CK_STATESAVE_ANIMATION | 0x00002000 | object sequence of CKObjectAnimation; only if the entity has animations or its chunk had the section |
| CK_STATESAVE_3DENTITYNDATA | 0x00100000 | dword entity flags (CK_3DENTITY_FLAGS), dword moveable flags (VX_MOVEABLE_FLAGS), 4 vectors: rows 0-3 of the world matrix (the fourth column is implied 0, 0, 0, 1); if flags & 0x00010000 (place valid): object id of the CKPlace; if flags & 0x00020000 (parent valid): object id of the parent CK3dEntity; if flags & 0x00100000 (z-order valid): int z-order |
| CK_STATESAVE_PARENT | 0x00008000 | legacy, written back only for an object loaded from it: object id of the parent CK3dEntity |
| CK_STATESAVE_3DENTITYFLAGS | 0x00010000 | legacy, likewise: dword entity flags, dword moveable flags |
| CK_STATESAVE_3DENTITYMATRIX | 0x00020000 | legacy, likewise: dword (old matrix byte size, skipped by the engine), matrix world |
| CK_STATESAVE_3DENTITYSKINDATA | 0x00200000 | only if the entity has a skin: matrix object initial matrix; object sequence of bones (CK3dEntity); per bone: dword bone flags, matrix inverse initial matrix; int vertex count; per vertex: int bone count n, vector initial position, raw n floats weights, raw n dwords bone indices |
| CK_STATESAVE_3DENTITYSKINDATANORMALS | 0x00001000 | only with a skin that has normals: raw vertex count x vector (no count; the reader takes 12 bytes per skin vertex) |

Notes: memory mode writes nothing past the CKBeObject sections unless the save flags include a CK_STATESAVE_3DENTITYONLY bit (0x003FF000). The writer sets or clears the three "valid" flag bits from the state. New objects write NDATA; PARENT, FLAGS and MATRIX are read only when NDATA is absent, and an object loaded from them is written back with them and no NDATA. Skin at data version < 6: one extra dword before the object initial matrix, before each bone matrix, and before each vertex's position, weights and indices (the engine skips them; their meaning is unclear, likely old size prefixes); libnmo keeps them and refuses to write nonzero ones at version >= 6. A new CK3dObject is saved with chunk class id 33 (src/object/object_system.c:158).

## CKCamera (class id 34)

Parent: CK3dEntity. Data version: set to current (10) when the chunk has 0, unless a legacy layout is being kept; the layout branches at 5.

| Identifier | Value | Contents |
|---|---|---|
| | | the CK3dEntity sections |
| CK_STATESAVE_CAMERAONLY | 0x0FC00000 | dword projection type (1 perspective, 2 orthographic), float field of view, float orthographic zoom, dword aspect ratio (width in the low 16 bits, height in the high 16), float near plane, float far plane |

Notes: memory mode writes nothing past the CK3dEntity sections unless the save flags include a CK_STATESAVE_CAMERAONLY bit. In file mode the section is written for new cameras and for cameras whose chunk had it; a loaded camera without it must still hold the defaults (perspective, 0.5, 1, 4:3, 1, 4000). Data version < 5 (read; written back in file mode for a chunk kept below 5): separate sections, each only if loaded: CAMERAFOV 0x00400000 (float), CAMERAPROJTYPE 0x00800000 (dword), CAMERAOTHOZOOM 0x01000000 (float), CAMERAASPECT 0x02000000 (int width, int height), CAMERAPLANES 0x04000000 (float near, float far).

## CKTargetCamera (class id 35)

Parent: CKCamera. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKCamera sections |
| CK_STATESAVE_TCAMERATARGET | 0x10000000 | object id of the target (CK3dEntity; null when none) |

Notes: always written in file mode; memory mode writes it only when the save flags include a CK_STATESAVE_TCAMERAONLY bit (0x70000000).

## CKCurvePoint (class id 36)

Parent: CK3dEntity. Data version: none of its own; the layout branches at 5.

| Identifier | Value | Contents |
|---|---|---|
| | | the CK3dEntity sections |
| CK_STATESAVE_CURVEPOINTDEFAULTDATA | 0x10000000 | object id of the CKCurve, int use TCB, int linear; data version >= 5: float tension, float continuity, float bias, vector tangent in, vector tangent out; data version < 5: vector position |
| CK_STATESAVE_CURVEPOINTTCB | 0x20000000 | float tension, float continuity, float bias; only for a point loaded with it, or when a point read with the >= 5 layout is written at < 5 |
| CK_STATESAVE_CURVEPOINTTANGENTS | 0x40000000 | vector tangent in, vector tangent out; same condition as TCB |
| CK_STATESAVE_CURVEPOINTCURVEPOS | 0x80000000 | vector (reserved); only for a point loaded with it |

Notes: memory mode writes nothing past the CK3dEntity sections unless the save flags include a CK_STATESAVE_CURVEONLY bit (0xFFC00000, the mask libnmo and the engine use for curve points). The reader also takes TCB and TANGENTS at data version >= 5 when present; they override the DEFAULTDATA values. Below 5 the engine applies the DEFAULTDATA position after the entity data.

## CKSprite3D (class id 37)

Parent: CK3dEntity. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CK3dEntity sections |
| CK_STATESAVE_SPRITE3DDATA | 0x00400000 | dword mode (VXSPRITE3D_TYPE: 0 billboard, 1 X rotate, 2 Y rotate, 3 orientable), float half width, float half height, float offset x, float offset y, 4 floats UV rect (left, top, right, bottom), object id of the CKMaterial |

Notes: written for new sprites and for sprites whose chunk had it; memory mode writes it only when the save flags include a CK_STATESAVE_SPRITE3DONLY bit (0x0FC00000).

## CKLight (class id 38)

Parent: CK3dEntity. Data version: set to current (10) when the chunk has 0, unless a legacy layout is being kept; the layout branches at 5.

| Identifier | Value | Contents |
|---|---|---|
| | | the CK3dEntity sections |
| CK_STATESAVE_LIGHTDATA | 0x00400000 | dword type and flags (low byte VXLIGHT_TYPE: 1 point, 2 spot, 3 directional; upper 24 bits light flags: 0x100 active, 0x200 specular), dword diffuse color (packed ARGB, alpha written as 0xFF), float constant attenuation, float linear attenuation, float quadratic attenuation, float range; spot lights only: float outer cone, float inner cone, float falloff |
| CK_STATESAVE_LIGHTDATA2 | 0x00800000 | float power; only when the power is not 1 or the chunk had the section |

Notes: memory mode writes nothing past the CK3dEntity sections unless the save flags include a CK_STATESAVE_LIGHTONLY bit (0x0FC00000), and then always writes LIGHTDATA; file mode leaves it out for a loaded light that had none and still holds the defaults. A type byte outside 1..3 is read as point; without LIGHTDATA2 the power is 1. Data version < 5 (read; written back in file mode for a chunk kept below 5): LIGHTDATA is 14 dwords: dword type, float r, g, b, float a (kept, unused), int active, int specular, 3 floats attenuation, float range, float outer cone, float inner cone, float falloff (spot values present for every type); no LIGHTDATA2.

## CKTargetLight (class id 39)

Parent: CKLight. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKLight sections |
| CK_STATESAVE_TLIGHTTARGET | 0x80000000 | object id of the target (CK3dEntity; null when none) |

Notes: always written in file mode; memory mode writes it only when the save flags include a CK_STATESAVE_TLIGHTONLY bit (0xF0000000).

## CKCharacter (class id 40)

Parent: CK3dEntity. Data version: none of its own; the layout branches at 5: data version >= 5 is the current layout, below 5 the legacy one (read, and written back for an object whose chunk keeps that version).

| Identifier | Value | Contents |
|---|---|---|
| | | the CK3dEntity sections |
| CK_STATESAVE_CHARACTERBODYPARTS | 0x00400000 | object sequence of CKBodyPart (data version < 5: file mode only) |
| CK_STATESAVE_CHARACTERSAVEPARTS | 0x10000000 | memory mode with CK_STATESAVE_CHARACTERSAVEPARTS in the save flags: sub-chunk sequence, one CKBodyPart state chunk per body part (the count must equal the body part count); data version < 5: dword count, then count x (object id CKBodyPart, sub-chunk) |
| CK_STATESAVE_CHARACTERONLY | 0xFFC00000 | data version >= 5: file mode only: object sequence of CKAnimation (the character's animations); then object sequence of 4: active animation (CKKeyedAnimation), animation destination (CKAnimation), root body part (CKBodyPart), floor reference (CK3dEntity) |
| CK_STATESAVE_CHARACTERANIMATIONS | 0x01000000 | data version < 5, file mode: object sequence of CKAnimation, object id active animation (CKKeyedAnimation), object id animation destination (CKAnimation) |
| CK_STATESAVE_CHARACTERSAVEANIMS | 0x04000000 | data version < 5, memory mode: dword (read and dropped), object id active animation (CKKeyedAnimation), object id animation destination (CKAnimation) |
| CK_STATESAVE_CHARACTERROOT | 0x02000000 | data version < 5, only when not null: object id root body part (CKBodyPart) |
| CK_STATESAVE_CHARACTERFLOORREF | 0x20000000 | data version < 5, only when not null: object id floor reference (CK3dEntity) |

Notes: memory mode writes nothing past the CK3dEntity sections unless the save flags include a CK_STATESAVE_CHARACTERONLY bit (0xFFC00000). Data version >= 5 write order: BODYPARTS, SAVEPARTS, CHARACTERONLY; libnmo also writes SAVEPARTS in file mode when the loaded chunk had it (the engine never does). Data version < 5 write order: file mode BODYPARTS, ANIMATIONS, ROOT, FLOORREF; memory mode SAVEANIMS, SAVEPARTS, ROOT, FLOORREF. Unclear: the meaning of the leading dword of SAVEANIMS.

## CK3dObject (class id 41)

Parent: CK3dEntity. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CK3dEntity sections |

Notes: no sections of its own. A new CK3dObject is saved with chunk class id 33 (CK3dEntity), as the engine's inherited CK3dEntity::Save does; a loaded one keeps its chunk's class id.

## CKBodyPart (class id 42)

Parent: CK3dObject. Data version: none of its own; the layout branches at 5: below 5 the legacy layout is read, and written back for an object whose chunk keeps that version.

| Identifier | Value | Contents |
|---|---|---|
| | | the CK3dObject sections |
| CK_STATESAVE_BODYPARTROTJOINT | 0x01000000 | data version < 5 only: buffer of 72 bytes: 3 x 3 dword booleans (rows active, limited, eased; columns X, Y, Z), vector min, vector max, vector damping |
| CK_STATESAVE_BODYPARTCHARACTER | 0x04000000 | object id character (CKCharacter); data version >= 5 and entity flags with CK_3DENTITY_IKJOINTVALID (0x00040000): then raw buffer of 40 bytes, the rotation joint: dword joint flags, vector min, vector max, vector damping |

Notes: memory mode writes nothing past the CK3dObject sections unless the save flags include a CK_STATESAVE_BODYPARTONLY bit (0x7F000000). libnmo writes CHARACTER (and ROTJOINT) only when the state holds them (from the loaded chunk); the engine writes CHARACTER on every file save. The legacy booleans map to the joint flags 0x1/0x10/0x100 shifted by the axis (active/limit/ease); libnmo refuses to write flags the 72-byte block cannot hold. A ROTJOINT buffer larger than 72 bytes is skipped.

## CKCurve (class id 43)

Parent: CK3dEntity. Data version: none of its own; the layout branches at 5.

| Identifier | Value | Contents |
|---|---|---|
| | | the CK3dEntity sections (without MESHS, which CK3dEntity never writes for a curve chunk) |
| CK_STATESAVE_CURVEONLY | 0xFFC00000 | object sequence of the control points (CKCurvePoint), float fitting coefficient, dword step count, dword open (0 = closed) |
| CK_STATESAVE_CURVESAVEPOINTS | 0xFF000000 | dword count, then count x (object id of a CKCurvePoint, sub-chunk: that point's state chunk); memory mode, and in file mode only for a curve whose file chunk had it |

Notes: memory mode writes nothing past the CK3dEntity sections unless the save flags include a CK_STATESAVE_CURVEONLY bit. The engine writes SAVEPOINTS only when not saving into a file. Data version < 5 (read; written for a chunk kept below 5): separate sections CK_STATESAVE_CURVECONTROLPOINT 0x00800000 (object sequence of CKCurvePoint), CK_STATESAVE_CURVEFITCOEFF 0x00400000 (float), CK_STATESAVE_CURVESTEPS 0x01000000 (dword), CK_STATESAVE_CURVEOPEN 0x02000000 (dword). Without them the constructor values stay (100 steps, open).

## CKParameterLocal (class id 45)

Parent: CKParameter. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKParameter sections; the CKObject sections only for a "myself" parameter, or in memory mode when the save flags lack CK_STATESAVE_PARAMETEROUT_VAL (0x40) |
| CK_STATESAVE_PARAMETEROUT_MYSELF | 0x00000200 | no data; present when the parameter stands for its owner object ("myself") |
| CK_STATESAVE_PARAMETEROUT_ISSETTING | 0x00000400 | no data; present when the parameter is a setting |

Notes: uses the CKParameterOut identifiers. Memory mode writes nothing after the base when the save flags are 0, and MYSELF / ISSETTING only when the save flags include their bit.

## CKParameter (class id 46)

Parent: CKObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |
| CK_PARAM_IDENTIFIER | 0x00000040 | GUID parameter type; then, unless the parameter is header-only, dword mode and the value: 0 = sub-chunk written by the type's save/load function; 1 = buffer of the raw value bytes, except for type CKPGUID_PARAMETERTYPE (0x34517DF5, 0x045E4965), which stores the GUID of the referenced type; 2 = object id (types deriving from CKObject); 3 = no value; any other dword = manager int: that dword is d1 of the manager GUID, followed by d2 and dword value (written as a manager int, so the chunk's manager list records it) |

Notes: CK_PARAM_IDENTIFIER is a local define in ckparameter_schemas.c (same value as CK_STATESAVE_PARAMETEROUT_VAL). Header-only (the section ends after the GUID) is what the engine writes for an object flagged for file reference only; libnmo writes it when the state has no value. On read, the old message, attribute and time GUIDs and CKPGUID_ID (0x71653557, 0x2D1B2E97, mapped to CKPGUID_OBJECT) only select the class an object id is checked against; the stored GUID is written back unchanged. A manager GUID whose d1 is 0 to 3 cannot be stored (validation rejects it). No file/memory mode difference.

## CKRenderObject (class id 47)

Parent: CKBeObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKBeObject sections |

Notes: no sections of its own in either mode (the engine has no CKRenderObject::Save either).

## CKInterfaceObjectManager (class id 48)

Parent: CKObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |
| CK_STATESAVE_IOM_CHUNKS | 0x01234567 | int count, then count x sub-chunk (opaque interface data, not parsed) |
| CK_STATESAVE_IOM_GUID | 0x87654321 | GUID |

Notes: both identifiers are local `#define`s in ckinterfaceobjectmanager_schemas.c. Both modes use the same layout. A new object writes both sections (count 0, null GUID). A loaded object writes only the sections its source chunk had.

## CKCriticalSectionObject (class id 49)

Parent: CKObject. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |
| CK_STATESAVE_SYNCHRODATA | 0x00000010 | object id of the object in the section (CKBeObject) |

Notes: written in file and memory mode alike. libnmo rejects a SYNCHRODATA section that is not exactly 1 dword.

## CKGrid (class id 50)

Parent: CK3dEntity. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| | | the CK3dEntity sections |
| CK_STATESAVE_GRIDDATA | 0x00400000 | int width, int length, int reserved (engine writes 0), int priority, dword orientation mode; file mode only: int (engine writes 1); object sequence of CKLayer ids; memory mode only: one sub-chunk per layer, in sequence order, with no count of its own |

Notes: memory mode writes GRIDDATA only when the save flags include a CK_STATESAVE_GRIDONLY bit (0x0FC00000). Each memory-mode layer sub-chunk is that layer's own memory-mode chunk (CKLayer layout). Unclear: what the file-mode int means. The engine writes 1 and skips it on read (Ballanced CKGrid.cpp:141). libnmo writes back the value it read, or 1 (ckgrid_schemas.c:290).

## CKLayer (class id 51)

Parent: CKObject. Data version: not used; in file mode LAYERDATA carries its own int layer version (0-3). libnmo writes back the loaded version, or 3 for a new layer, and raises it when the layer holds a field the older layout lacks (cklayer_schemas.c:321).

| Identifier | Value | Contents |
|---|---|---|
| | | the CKObject sections |
| CK_STATESAVE_LAYERDATA | 0x00000010 | object id: owning CKGrid; memory mode: int layer type, int format, int flags; file mode: int format, int layer version, layer version >= 1: dword color (packed), layer version >= 3: GUID parameter type, layer version >= 2: int flags; then, if format == 0 and the layer has a grid (or square data): buffer of dwords, one per grid cell |

Notes: memory mode writes LAYERDATA only when the save flags include CK_STATESAVE_LAYERDATA. In file mode the GUID comes before the flags. Below layer version 2 libnmo assumes flags = 1, and below 3 it assumes parameter type CKPGUID_INT. The file-mode chunk stores no layer type. The engine derives the type from the layer's name on load, in both modes, and ignores the memory-mode type int (Ballanced CKLayer.cpp:326). The reader takes the square buffer only when format == 0 and the section has dwords left after the header. When the owning grid is resolvable, libnmo sizes the buffer to width x length cells, zero-padding or truncating it (cklayer_schemas.c:354).

## CKDataArray (class id 52)

Parent: CKBeObject. Data version: none of its own; the key column depends on data version 5 in memory mode.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKBeObject sections |
| CK_STATESAVE_DATAARRAYFORMAT | 0x00001000 | int column count, then per column: string name, int type (CK_ARRAYTYPE: 1 int, 2 float, 3 string, 4 object, 5 parameter), GUID parameter type if type 5 |
| CK_STATESAVE_DATAARRAYDATA | 0x00002000 | int row count, then per row, one cell per column in column order: int, float, string, object id (any CKObject), or for a parameter column: file mode: object id of the CKParameterOut; memory mode: sub-chunk holding that parameter's own state (a single 0 dword when none) |
| CK_STATESAVE_DATAARRAYMEMBERS | 0x00004000 | int order, dword column index, int key column (file mode, or data version >= 5) |

Notes: all three sections are written in both modes; rows carry no per-row count. A column GUID equal to CKPGUID_OLDTIME (0x4a4d4867, 0x3c28773f) is read as CKPGUID_TIME (0x54b4422b, 0x730f0f4f); libnmo writes the old GUID back for a column it read that way. Memory mode with data version < 5 has no key column. libnmo rejects FORMAT, DATA and MEMBERS sections with dwords left over.

## CKPatchMesh (class id 53)

Parent: CKMesh. Data version: none of its own; the CKMesh call runs without geometry and so only sets current (10) when 0, keeping a re-saved chunk's version even below 9. DATA3 and DATA2 are told apart by identifier, not by version.

| Identifier | Value | Contents |
|---|---|---|
| | | the CKMesh sections without geometry: the CKBeObject sections, MESHFLAGS, and MESHWEIGHTS, MESHFACECHANMASK, PROGRESSIVEMESH if the state has them; never MESHMATERIALS, MESHFACES, MESHLINES, MESHVERTICES or MESHCHANNELS |
| CK_STATESAVE_PATCHMESHDATA3 | 0x08000000 | dword patch flags (CK_PATCHMESH_FLAGS), int iteration count, int vec count, dword byte size (n x 12), dword n (verts + vecs), raw n x vector; object sequence (CKMaterial), one per patch; then per patch: dword type (3 tri, 4 quad), dword smoothing group, raw 40 bytes = 20 words (v[4], vec[8], interior[4], edge[4]); dword byte size (e x 12), dword e, raw e x 6 words edges (v1, vec12, vec21, v2, patch1, patch2); object sequence (CKMaterial), one per texture channel; then per channel: 3 dwords (see notes), dword byte size (t x 8), dword t, raw t x 4 words texture-vertex indices, dword byte size (u x 8), dword u, raw u x (float u, float v) |
| CK_STATESAVE_PATCHMESHDATA2 | 0x01000000 | obsolete, written instead of DATA3 only when the source had it: dword patch flags, object id (CKMaterial, default material), int iteration count, int vec count, dword byte size, dword n, raw n x vector; then four arrays, each dword byte size, dword count, raw bytes: 88-byte legacy patch records, 24-byte legacy edge records, 16-byte legacy texture patches, (float u, float v) UVs |
| CK_STATESAVE_PATCHMESHSMOOTH | 0x02000000 | obsolete, with DATA2 only: dword byte size, dword count, raw count dwords smoothing groups (one per patch) |
| CK_STATESAVE_PATCHMESHMATERIALS | 0x04000000 | obsolete, with DATA2 only: object sequence (CKMaterial), one per patch |

Notes: memory mode writes the CKMesh part under CKMesh's rule (CK_STATESAVE_MESHONLY bits) and the patch sections only if the save flags include CK_STATESAVE_PATCHMESHONLY (0x0FF00000) bits. A patch mesh without loaded patch data (new object, format none) writes no patch section. When reading class 53 at data version >= 9, the CKMesh reader skips MESHMATERIALS, MESHVERTICES, MESHFACES, MESHLINES and MESHCHANNELS (the engine skips them at every version; libnmo's < 9 reader does not). The obsolete CK_STATESAVE_PATCHMESHDATA (0x00800000) is read by neither libnmo nor Ballanced and has the same value as CK_STATESAVE_PROGRESSIVEMESH. Unclear: the meaning of the three per-channel dwords in DATA3; libnmo names them source blend, dest blend, flags, Ballanced names them Flags, Type, SubType, and libnmo notes that channel 0 is the base texture-coordinate set whose dwords are uninitialised memory in files.

## CKObjectArray (class id 80)

No schema: only the class id is defined. Not a CKObject class; libnmo has no chunk layout for it.

## CKSceneObjectDesc (class id 81)

No schema: only the class id is defined. A scene's per-object descriptions are stored inside CKScene's SCENENEWDATA section (see CKScene).

## CKAttributeManager (class id 82)

Manager data chunk, not an object chunk: the schema registers class id 0, no parent, and manager GUID NMO_MANAGER_GUID_ATTRIBUTE {0x3D242466, 0x00000000}. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| CK_STATESAVE_ATTRIBUTEMANAGER | 0x00000052 | int category count C, int attribute count A; C x (int present; if present: string name, dword flags); A x (int present; if present: string name, GUID parameter type, int category index, int compatible class id, dword flags) |

Notes: the identifier is a local `#define`. Entries are indexed by position: a slot with present = 0 is a gap, and attribute types stored elsewhere in the file as manager ints under this GUID index this table. The engine remaps them through it on load. The section is always written, even with zero counts. The reader requires it to end exactly after the last entry. The engine creates the chunk with class id 82 (Ballanced CKAttributeManager.cpp:818).

## CKMessageManager (class id 83)

Manager data chunk, not an object chunk: the schema registers class id 0, no parent, and manager GUID NMO_MANAGER_GUID_MESSAGE {0x466A0FAC, 0x00000000}. Data version: none of its own.

| Identifier | Value | Contents |
|---|---|---|
| CK_STATESAVE_MESSAGEMANAGER | 0x00000053 | int count, then count x string message type name (indexed by message type) |

Notes: the identifier is a local `#define`. libnmo writes no section when the count is 0; the engine writes no chunk when the file uses no message type. Message types stored elsewhere as manager ints under this GUID index this list. Unclear: how an unused type is encoded. libnmo's reader comment says a null string (ckmessagemanager_schemas.c:142), while Ballanced writes "" (CKMessageManager.cpp:227). libnmo accepts both and round-trips them.

## CKCollisionManager (class id 84)

No schema: only the class id (a manager class) is defined; libnmo has no class schema for its data.

## CKObjectManager (class id 85)

No schema: only the class id (a manager class) is defined; libnmo has no class schema for its data.

## CKFloorManager (class id 86)

No schema: only the class id (a manager class) is defined; libnmo has no class schema for its data.

## CKRenderManager (class id 87)

No schema: only the class id (a manager class) is defined; libnmo has no class schema for its data.

## CKBehaviorManager (class id 88)

No schema: only the class id (a manager class) is defined; libnmo has no class schema for its data.

## CKInputManager (class id 89)

No schema: only the class id (a manager class) is defined; libnmo has no class schema for its data.

## CKParameterManager (class id 90)

No schema: only the class id (a manager class) is defined; libnmo has no class schema for its data.

## CKGridManager (class id 91)

No schema: only the class id (a manager class) is defined; libnmo has no class schema for its data.

## CKSoundManager (class id 92)

No schema: only the class id (a manager class) is defined; libnmo has no class schema for its data.

## CKTimeManager (class id 93)

No schema: only the class id (a manager class) is defined; libnmo has no class schema for its data.
