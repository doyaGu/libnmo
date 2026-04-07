#ifndef NMO_REPL_UTIL_H
#define NMO_REPL_UTIL_H

#include "nmo_repl_types.h"

#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void nmo_repl_print_prompt(const nmo_repl_context_t *repl);

int nmo_repl_parse_command(char *line, char **argv, int max_args);

void nmo_repl_get_objects(nmo_repl_context_t *repl, nmo_object_t ***objects, size_t *count);

void nmo_repl_print_object_summary(const nmo_repl_context_t *repl, size_t index, nmo_object_t *obj);
void nmo_repl_print_object_summary_marked(const nmo_repl_context_t *repl,
                                          size_t index,
                                          nmo_object_t *obj,
                                          bool selected);

bool nmo_repl_paginate_if_needed(nmo_repl_context_t *repl, size_t printed);

bool nmo_repl_parse_u32(const char *text, uint32_t *out);
bool nmo_repl_parse_size(const char *text, size_t *out);

int nmo_repl_resolve_object_index(nmo_repl_context_t *repl,
                                  const char *selector,
                                  size_t *out_index,
                                  bool allow_default);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REPL_UTIL_H */
