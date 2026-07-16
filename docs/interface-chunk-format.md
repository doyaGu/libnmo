# InterfaceChunk layout notes

libnmo accepts InterfaceChunk versions `0x12` through `0x16` in inline and
sectioned layouts.

For sectioned data, the low layout index is added to these neutral section tags:

| Tag base | Stored data |
| --- | --- |
| `0xB0040000` | local parameter coordinate pairs and styles |
| `0xB0050000` | two input graph mapping tables of `(value, tag)` records |
| `0xB0060000` | two output graph mapping tables of `(value, tag)` records |
| `0xB0090000` | shared parameter coordinates, styles, and raw tagged mappings |

The mapping tags are intentionally exposed as `mapping_tag0`, `mapping_tag1`,
`mapping_value`, and graph `*_tags`. Their business meaning is not inferred.

Parsed sectioned chunks set `NMO_INTERFACE_FORMAT_SECTION_PRESENCE`. The writer
then preserves absent versus present-empty sections, header layout, and unknown
flag values. A newly authored sectioned value without that flag receives the
default section set.
