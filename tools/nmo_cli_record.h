/**
 * @file nmo_cli_record.h
 * @brief Format-neutral output records for CLI commands.
 *
 * A command describes what it wants to show once, as an ordered list of typed
 * fields, and the record renders itself either as a JSON object (yyjson) or as
 * human-readable text (key/value lines or a table row). Each field carries a
 * JSON key, a text label, and a value; either side may be omitted so that the
 * two presentations can differ where they historically did (for example a
 * combined "ID / Name" line in text next to separate "id" and "name" keys in
 * JSON) without duplicating the value-gathering code.
 */
#ifndef NMO_CLI_RECORD_H
#define NMO_CLI_RECORD_H

#include "yyjson.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_cli_record nmo_cli_record_t;

/** Create an empty record. Returns NULL on allocation failure. */
nmo_cli_record_t *nmo_cli_record_new(void);

/** Free a record and every child record it owns. NULL is ignored. */
void nmo_cli_record_free(nmo_cli_record_t *record);

/** Number of fields added so far. */
size_t nmo_cli_record_field_count(const nmo_cli_record_t *record);

/*
 * Field adders. `key` is the JSON key (NULL: not emitted in JSON); `label` is
 * the text label (NULL: not emitted in text). Strings are copied. All adders
 * return false on allocation failure; the record stays usable.
 */
bool nmo_cli_record_uint(nmo_cli_record_t *record, const char *key,
                         const char *label, uint64_t value);
bool nmo_cli_record_int(nmo_cli_record_t *record, const char *key,
                        const char *label, int64_t value);
/** `text_format` is the printf format used for the text side, e.g. "%.3f". */
bool nmo_cli_record_real(nmo_cli_record_t *record, const char *key,
                         const char *label, double value,
                         const char *text_format);
bool nmo_cli_record_bool(nmo_cli_record_t *record, const char *key,
                         const char *label, bool value);
/** Emits the string on both sides. `value` NULL is written as an empty string. */
bool nmo_cli_record_str(nmo_cli_record_t *record, const char *key,
                        const char *label, const char *value);
/**
 * Emits `value` when it is a non-empty string. Otherwise the JSON field is
 * omitted and the text shows `text_fallback` (NULL: the text line is omitted
 * too).
 */
bool nmo_cli_record_str_opt(nmo_cli_record_t *record, const char *key,
                            const char *label, const char *value,
                            const char *text_fallback);
/** "0x%08X" on both sides. */
bool nmo_cli_record_hex32(nmo_cli_record_t *record, const char *key,
                          const char *label, uint32_t value);
/**
 * Three-component vector. JSON: an object with "x", "y", "z" reals. Text:
 * "(x, y, z)" with each component printed using `component_format`
 * (NULL: "%.4f").
 */
bool nmo_cli_record_vec3(nmo_cli_record_t *record, const char *key,
                         const char *label, double x, double y, double z,
                         const char *component_format);
/** JSON null; text shows `text` (NULL: omitted). */
bool nmo_cli_record_null(nmo_cli_record_t *record, const char *key,
                         const char *label, const char *text);
/** Text-only line. */
bool nmo_cli_record_text(nmo_cli_record_t *record, const char *label,
                         const char *text);
/**
 * Text-only block written verbatim (no label, no trailing newline added).
 * Use for pre-formatted multi-line output.
 */
bool nmo_cli_record_raw(nmo_cli_record_t *record, const char *text);
/**
 * Text-only section heading: a blank line followed by `title` in the heading
 * style (bold when colorized). Absent from JSON and from table cells.
 */
bool nmo_cli_record_heading(nmo_cli_record_t *record, const char *title);
/**
 * Hex dump of a byte buffer, limited to `max_bytes` (0: no limit). JSON: the
 * "data_hex" / "data_emit_size" / "data_truncated" / "data_total_size" keys
 * of nmo_cli_json_add_data_hex, nothing when `size` is zero. Text: a
 * "<label>: (empty)" line for an empty buffer, a "<label>: showing N/M bytes"
 * line when truncated, then a canonical hex dump. The bytes are copied.
 */
bool nmo_cli_record_hex_bytes(nmo_cli_record_t *record, const char *label,
                              const void *bytes, size_t size,
                              size_t max_bytes);
/**
 * JSON array of reals. Text shows `text` (NULL: omitted).
 */
bool nmo_cli_record_real_list(nmo_cli_record_t *record, const char *key,
                              const char *label, const double *values,
                              size_t count, const char *text);
/**
 * JSON array of strings; NULL entries become JSON null. Text shows `text`
 * (NULL: omitted).
 */
bool nmo_cli_record_str_list(nmo_cli_record_t *record, const char *key,
                             const char *label, const char *const *values,
                             size_t count, const char *text);
/**
 * JSON array of unsigned integers. Text shows `text` (NULL: omitted).
 */
bool nmo_cli_record_uint_list(nmo_cli_record_t *record, const char *key,
                              const char *label, const uint64_t *values,
                              size_t count, const char *text);
/**
 * Object reference. JSON: `id_key` as an unsigned integer and, when `name`
 * is non-empty, `name_key` as a string (either key may be NULL to skip it).
 * Text: "#<id> (<name>)", "#<id>" when unnamed, or `none_text` when the id is
 * zero (NULL: the text line is omitted for a zero id).
 */
bool nmo_cli_record_ref(nmo_cli_record_t *record, const char *id_key,
                        const char *name_key, const char *label,
                        uint64_t id, const char *name, const char *none_text);
/** Like nmo_cli_record_ref, but emits nothing in JSON when the id is zero. */
bool nmo_cli_record_ref_opt(nmo_cli_record_t *record, const char *id_key,
                            const char *name_key, const char *label,
                            uint64_t id, const char *name,
                            const char *none_text);
/** Text-only line with a printf-formatted value of any length. */
bool nmo_cli_record_text_fmt(nmo_cli_record_t *record, const char *label,
                             const char *format, ...);
/**
 * Override the text of the most recently added field. Use when the text
 * presentation has a shape the typed adders cannot express.
 */
bool nmo_cli_record_set_text(nmo_cli_record_t *record, const char *text);
/** Like nmo_cli_record_set_text, formatted; the result may be any length. */
bool nmo_cli_record_set_text_fmt(nmo_cli_record_t *record, const char *format, ...);
/** Drop the JSON side of the most recently added field. */
void nmo_cli_record_text_only(nmo_cli_record_t *record);

/**
 * Nested array of records. The returned child list is owned by the record;
 * add items with nmo_cli_record_array_add(). In JSON the field becomes an
 * array of objects. In text, `label` (when non-NULL) is printed as
 * "\n<label> (<count>):\n" followed by one line per item using the item's
 * summary text (see nmo_cli_record_set_summary); items without a summary are
 * skipped in text.
 */
typedef struct nmo_cli_record_array nmo_cli_record_array_t;
nmo_cli_record_array_t *nmo_cli_record_array(nmo_cli_record_t *record,
                                             const char *key,
                                             const char *label);
/** Append an item; the array takes ownership of `item`. */
bool nmo_cli_record_array_add(nmo_cli_record_array_t *array,
                              nmo_cli_record_t *item);
size_t nmo_cli_record_array_count(const nmo_cli_record_array_t *array);
/**
 * Replace the default "\n<label> (<count>):\n" text heading with a literal
 * line (printed as "\n<heading>\n").
 */
bool nmo_cli_record_array_set_heading(nmo_cli_record_array_t *array,
                                      const char *heading);
/** Emit nothing, in JSON or text, when the array has no items. */
void nmo_cli_record_array_omit_empty(nmo_cli_record_array_t *array);
/** Text line printed under the heading when the array has no items. */
bool nmo_cli_record_array_set_empty_text(nmo_cli_record_array_t *array,
                                         const char *text);
/** One-line text used when this record is rendered as an array item. */
bool nmo_cli_record_set_summary(nmo_cli_record_t *record, const char *text);
/** Formatted variant of nmo_cli_record_set_summary. */
bool nmo_cli_record_set_summary_fmt(nmo_cli_record_t *record,
                                    const char *format, ...);

/* Rendering */

/** Add every JSON-visible field to `obj` in insertion order. */
bool nmo_cli_record_to_json(const nmo_cli_record_t *record,
                            yyjson_mut_doc *doc, yyjson_mut_val *obj);
/** Print every text-visible field as "label: value" lines. */
void nmo_cli_record_print_kv(const nmo_cli_record_t *record, FILE *out,
                             int key_width, bool colorize);
/**
 * Collect the text of every text-visible field, in order, for use as table
 * cells. `cells` must have room for `capacity` pointers; returns the number
 * written. Pointers stay valid until the record is freed or modified.
 */
size_t nmo_cli_record_cells(const nmo_cli_record_t *record,
                            const char **cells, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CLI_RECORD_H */
