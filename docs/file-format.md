# The Virtools file format

This is the layout of Virtools composition files (`.nmo`, `.cmo`, `.vmo`, `.nms`) as the
Virtools 2.x engine (CK2) reads and writes them and as libnmo implements it. Where the engine
and libnmo differ, both are given. The layout of each class's state chunk is in
[file-format-classes.md](file-format-classes.md).

The code is the authority: `src/format/` (header, Header1, data section, chunks),
`src/session/deserializer.c` and `src/session/serializer.c` (load and save order), and
`src/object/builtin/` (class chunks).

## Conventions

- All integers are little-endian. A *dword* is 4 bytes.
- A GUID is two dwords, `d1` then `d2`, written `{D1-D2}` in hex.
- Sizes are in bytes unless they say dwords.
- *File version* is the `file_version` header field, 2 to 9. Virtools 2.x writes 8, and so
  does libnmo. Files of versions 2 to 7 load; libnmo saves them as version 8. Version 9
  loads but is never written down to 8.

## File layout

```
+------------------------+  32 bytes   header part 0
| header part 0          |
+------------------------+  32 bytes   header part 1 (file version 5 and later)
| header part 1          |
+------------------------+  hdr1_pack_size bytes (file version 7 and later)
| Header1                |  object table, plugin dependencies, included file list
+------------------------+  data_pack_size bytes (to the end of the file below version 5)
| data section           |  manager chunks, then object chunks
+------------------------+
| included files         |  as many as Header1 lists
+------------------------+
```

Header1 and the data section are each zlib streams when the file is compressed (see
[Compression](#compression)).

## Header

| Offset | Field | Notes |
|---:|---|---|
| 0 | `signature[8]` | `"Nemo Fi\0"` |
| 8 | `crc` | Checksum, see [Checksum](#checksum) |
| 12 | `ck_version` | Engine version that wrote the file |
| 16 | `file_version` | 2 to 9 |
| 20 | `file_version2` | 0; libnmo refuses anything else |
| 24 | `file_write_mode` | `CK_FILE_WRITEMODE` flags, below |
| 28 | `hdr1_pack_size` | Header1 bytes as stored |
| 32 | `data_pack_size` | Data section bytes as stored |
| 36 | `data_unpack_size` | Data section bytes unpacked |
| 40 | `manager_count` | Manager chunks in the data section |
| 44 | `object_count` | Objects in the Header1 object table |
| 48 | `max_id_saved` | Highest object id the file uses |
| 52 | `product_version` | |
| 56 | `product_build` | |
| 60 | `hdr1_unpack_size` | Header1 bytes unpacked |

Part 1 (offsets 32 to 63) is present from file version 5. Below version 5 the data section
starts at offset 32 and runs to the end of the file, and there is no Header1.

`file_write_mode`:

| Value | Name | Meaning |
|---:|---|---|
| 0 | `CKFILE_UNCOMPRESSED` | Header1 and the data section are stored as they are |
| 1 | `CKFILE_CHUNKCOMPRESSED_OLD` | Below version 4, object entries may be zlib packed one by one |
| 2 | `CKFILE_EXTERNALTEXTURES_OLD` | Old flag; textures refer to files |
| 4 | `CKFILE_FORVIEWER` | Saved for the web player |
| 8 | `CKFILE_WHOLECOMPRESSED` | Header1 and the data section are zlib streams |

## Checksum

From file version 8, `crc` is the Adler-32 of, in order:

1. header part 0 with `crc` set to 0 (32 bytes),
2. header part 1 (32 bytes, file version 5 and later),
3. Header1 as stored (packed),
4. the data section as stored (packed).

Below version 8 it is the Adler-32 of the unpacked data section alone. Below version 2 the
engine only warns. The engine refuses a file whose checksum does not match
(`CKERR_FILECRCERROR`); libnmo warns and records the status in the file info, and
`NMO_LOAD_VERIFY_CRC` makes a mismatch an error.

## Compression

When Header1 or the data section is stored with a size that differs from its unpacked size,
it is a zlib stream (`compress2` / `uncompress`) of the unpacked bytes. With
`CKFILE_WHOLECOMPRESSED` both are compressed. libnmo compresses each when its save options
ask for it (`compress_header`, `compress_data`, `compression_level`) and keeps the stream
only when it is smaller.

A deflate stream inflates to at most 1032 times its size (a 258-byte match costs at least two
bits). libnmo refuses a declared unpacked size above that before allocating for it, and holds
the unpacked sizes to the load limits (`max_unpacked_size`, `max_object_count` in
`nmo_load_options_t`).

## Header1

Header1 exists from file version 7. Its parts follow each other with no padding:

### Object table

`object_count` (from the header) entries:

| Field | Size | Notes |
|---|---|---|
| `object` | dword | The object's id in the file. Bit 31 set: the object is saved as a reference only, with no chunk; the engine looks for an existing object of that name and class instead of creating one |
| `class_id` | dword | Class id ([class ids](file-format-classes.md)) |
| `file_index` | dword | Byte offset of the object's entry in the unpacked data section |
| `name_length` | dword | Bytes of the name, no terminator; 0 for no name |
| `name` | `name_length` bytes | In the writer's code page (libnmo reads names as Latin-1 when they are not UTF-8) |

An object's *index* is its position in this table, from 0. Chunks refer to objects by index,
not by id (see [Object references](#object-references)).

### Plugin dependencies

Present when bytes remain after the object table:

| Field | Size |
|---|---|
| `category_count` | dword |
| then per category: `category` | dword |
| `guid_count` | dword |
| `guids` | `guid_count` × 8 bytes |

Categories are `CK_PLUGIN_TYPE`: 0 bitmap reader, 1 sound reader, 2 model reader, 3 manager,
4 behavior DLL, 5 render engine, 6 movie reader, 7 extension. The behavior category lists the
building block DLLs and then the prototype GUID of each building block in the file. The
engine computes the list on save from what it saves (`CKPluginManager::
ComputeDependenciesList`); libnmo keeps the loaded list, drops the prototypes of blocks no
longer saved and adds what new blocks and saved manager data need.

### Included file list

Present when bytes remain after the dependencies:

| Field | Size |
|---|---|
| `payload_size` | dword; bytes that follow |
| `file_count` | dword |
| rest of the payload | not used on load |

The files themselves follow the data section (see [Included files](#included-files)).

## Data section

Unpacked, the data section holds, in order:

1. **Leading counts**, below file version 8 only: `save_id_max` (dword) and `object_count`
   (dword). The engine takes these over the header's values.
2. **Manager entries**, from file version 6, `manager_count` of them:

   | Field | Size |
   |---|---|
   | `guid` | 8 bytes, the manager's GUID |
   | `size` | dword, bytes of the chunk |
   | `chunk` | `size` bytes, a [chunk](#chunks) |

3. **Object entries**, one per object of the table, in table order:

   | File version | Entry |
   |---|---|
   | 7 and later | `size` (dword), then `size` bytes of chunk |
   | 4 to 6 | `object` (dword, the object's id), `size`, chunk |
   | 2 and 3 | `object`, `size` (unpacked); when `size` is not 0: `class_id`, save flags, `stored_size`, then `stored_size` bytes |

   A `size` of 0 is an object with no chunk. For versions 2 and 3, with
   `CKFILE_CHUNKCOMPRESSED_OLD` and `stored_size` different from `size`, the stored bytes are
   a zlib stream; the bytes are the chunk's data dwords alone, not a chunk envelope, with chunk
   and data version 0.

Below file version 7 there is no Header1: the object table is built from the data section
(ids from the entries, classes and names from the chunks). At version 7 the section's count
may be lower than the header's, leaving the remaining objects without a chunk; it may not be
higher.

## Included files

After the data section, for each file Header1 lists:

| Field | Size |
|---|---|
| `name_length` | dword |
| `name` | `name_length` bytes |
| `size` | dword |
| `data` | `size` bytes |

libnmo caps `name_length` and `size` with `max_included_name_len` and
`max_included_file_size` (load options).

## Chunks

A chunk (`CKStateChunk`) is the saved state of one object or manager: a list of dwords with
three optional position lists. Every chunk in a file is written with the file's object table
in effect (file mode).

### Envelope

The first dword holds the versions. Its high 16 bits, low byte, are the *chunk version*; the
other bytes depend on it.

Chunk versions 4 and 5 (`CHUNK_VERSION1`, `CHUNK_VERSION2`), and below:

| Dword | Field |
|---|---|
| 0 | data version (low 16 bits), chunk version (high 16 bits) |
| 1 | class id |
| 2 | `data_size`, in dwords |
| 3 | reserved |
| 4 | `id_count` |
| 5 | `chunk_count` |
| 6 | `manager_count` (chunk version 5 only) |
| | `data_size` data dwords, then `id_count` id positions, `chunk_count` sub-chunk positions, `manager_count` manager positions |

Chunk versions 6 and 7 (`CHUNK_VERSION3`, `CHUNK_VERSION4`, the one written today):

| Byte of dword 0 | Field |
|---|---|
| 0 | data version |
| 1 | class id, low 8 bits |
| 2 | chunk version |
| 3 | options |

then `data_size` (dword, in dwords), the data dwords, and then a count dword and that many
positions for each list its option bit announces: ids, then sub-chunks, then manager values.

| Option | Value | List |
|---|---:|---|
| `CHNK_OPTION_IDS` | 0x01 | Positions of object ids |
| `CHNK_OPTION_MAN` | 0x02 | Positions of manager values |
| `CHNK_OPTION_CHN` | 0x04 | Positions of sub-chunks |
| `CHNK_OPTION_FILE` | 0x08 | Written in file mode |
| `CHNK_OPTION_ALLOWDYN` | 0x10 | May refer to dynamic objects |
| `CHNK_OPTION_LISTBIG` | 0x20 | Not used |

A position is a dword index into the data. In a list, the value `0xFFFFFFFF` marks a
sequence: the next value is the position of the sequence's count, and the items follow it.

The *data version* says which layout the class's sections use. The engine saves a new object
with data version 10 (`CHUNK_DEV_2_1`); some classes set their own value before writing, and
every class reads the older layouts it knows ([per class](file-format-classes.md)). Chunks
older than chunk version 4 keep object ids and sub-chunks in an older layout, which libnmo
reads as the engine does.

A chunk can be zlib packed as a whole in memory (`CHNK_OPTION_PACKED`); files store chunks
unpacked.

### Sections

The data is a chain of sections. Each starts with two dwords: the section's identifier
(`CK_STATESAVE_*`, defined per class) and the position of the next section's identifier, 0
for the last. A reader seeks an identifier by its exact value along the chain, starting after
the section it found last and wrapping around at the end, so sections a class does not know
are skipped and the order of the others does not matter. The values in a section follow without tags, in the order
the class writes them.

### Values

| Value | Encoding |
|---|---|
| byte, word, dword, int | One dword each (the value zero-extended) |
| float | One dword, IEEE 754 single |
| GUID | Two dwords |
| string | dword length in bytes including the terminator (0 for none), then the bytes padded to a dword |
| buffer | dword byte count, then the bytes padded to a dword |
| array | dword byte count, dword element count, then the bytes padded to a dword |
| object reference | One dword, see below |
| object sequence | dword count, then one dword per object |
| manager value | Manager GUID (two dwords), then the value; its position goes in the manager list |
| manager sequence | dword count, manager GUID, then the values |
| sub-chunk | An embedded chunk: dword size of what follows (in dwords), class id, versions (data version low 16 bits, chunk version high 16 bits), data size, file flag, id count, chunk count, manager count, then the data and the three lists. Its position goes in the sub-chunk list |
| sub-chunk sequence | dword count, then the sub-chunks (a missing one is a single 0 dword) |
| bitmap | See the texture and sprite sections in the class reference |

Floats keep their bits. Bytes after a string's terminator that pad it to a dword may hold
garbage in files written by the engine; libnmo writes zeros and treats such a difference as
none when it compares chunks.

### Object references

In a file, a chunk refers to an object by its index in the object table, or `0xFFFFFFFF` for
none. Positions of these references are not listed: a file-mode chunk has no id list
(`CHNK_OPTION_IDS` is clear), so a reader must know the layout to find them. On load the
engine (and libnmo, through the chunk's file context) turns each index into the id of the
object it loaded or created; on save it turns ids back into indices of the new table. An index
that names no object of the file is kept and written back as it was.

In memory (not file mode), a chunk refers to objects by runtime id and lists their positions.

## Load and save order

The engine loads by creating every object of the table, then reading every manager chunk,
then every object chunk in table order, then running the post-load steps
(`CKObject::PostLoad`, behavior and manager callbacks). libnmo does the same; see
`nmo_deserializer_parse_header`, `nmo_deserializer_parse_objects` and
`nmo_deserializer_finalize`, and the load profiles in `nmo_load_options_t` for partial loads.

A save writes the objects in the order of the new table, each chunk in file mode, then the
managers' chunks, computes the plugin dependencies, and writes header, Header1, data section
and included files. libnmo writes an unchanged object's loaded chunk back as it was and carries
what a schema does not model into the chunk of a changed object.
