#ifndef NMO_INSPECT_UTIL_H
#define NMO_INSPECT_UTIL_H

#include "nmo_inspect_types.h"

#include "core/nmo_guid.h"
#include "type/type_system.h"

#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void nmo_inspect_log(const inspect_options_t *opts, log_level_t level, const char *fmt, ...);

void nmo_inspect_warning_list_init(warning_list_t *warnings);
void nmo_inspect_warning_list_free(warning_list_t *warnings);
bool nmo_inspect_warning_list_add(warning_list_t *warnings, const char *code, const char *message, nmo_object_id_t object_id);

const char *nmo_inspect_safe_object_name(const nmo_object_t *object);

bool nmo_inspect_should_use_color(const inspect_options_t *opts, FILE *stream);
void nmo_inspect_print_heading(FILE *out, const inspect_options_t *opts, const char *title, bool colorize);

bool nmo_inspect_match_truncate(const inspect_options_t *opts, const char *value, char *buffer, size_t buffer_size);

const char *nmo_inspect_detect_container(const char *path);

nmo_type_registry_t *nmo_inspect_type_registry_from_state(const inspect_state_t *state);
nmo_class_id_t nmo_inspect_class_id_from_name(const inspect_state_t *state, const char *name);
const char *nmo_inspect_class_name_from_id(const inspect_state_t *state, nmo_class_id_t class_id);

bool nmo_inspect_object_matches_filters(const inspect_state_t *state, const inspect_options_t *opts, const nmo_object_t *object);
bool nmo_inspect_chunk_matches_filters(const inspect_filters_t *filters, uint32_t chunk_class_id, size_t chunk_index);

#ifdef __cplusplus
}
#endif

#endif /* NMO_INSPECT_UTIL_H */
