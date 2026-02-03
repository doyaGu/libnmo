#ifndef NMO_DEBUG_UTIL_H
#define NMO_DEBUG_UTIL_H

#include "nmo_debug_types.h"

#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void nmo_debug_print_prompt(const nmo_debug_context_t *dbg);

int nmo_debug_parse_command(char *line, char **argv, int max_args);

void nmo_debug_get_objects(nmo_debug_context_t *dbg, nmo_object_t ***objects, size_t *count);

const char *nmo_debug_class_name_from_id(const nmo_debug_context_t *dbg,
                                        nmo_class_id_t class_id,
                                        char *buffer,
                                        size_t buffer_size);

bool nmo_debug_class_id_from_name(const nmo_debug_context_t *dbg, const char *name, nmo_class_id_t *out_class_id);

void nmo_debug_print_object_summary(const nmo_debug_context_t *dbg, size_t index, nmo_object_t *obj);
void nmo_debug_print_object_summary_marked(const nmo_debug_context_t *dbg,
                                          size_t index,
                                          nmo_object_t *obj,
                                          bool selected);

bool nmo_debug_paginate_if_needed(nmo_debug_context_t *dbg, size_t printed);

bool nmo_debug_parse_u32(const char *text, uint32_t *out);
bool nmo_debug_parse_size(const char *text, size_t *out);

bool nmo_debug_regex_matches(const char *text, const char *pattern, bool icase);

void nmo_debug_json_write_string(FILE *out, const char *value);

int nmo_debug_resolve_object_index(nmo_debug_context_t *dbg,
                                  const char *selector,
                                  size_t *out_index,
                                  bool allow_default);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DEBUG_UTIL_H */
