#ifndef NMO_INSPECT_COLLECT_H
#define NMO_INSPECT_COLLECT_H

#include "nmo_inspect_types.h"

#ifdef __cplusplus
extern "C" {
#endif

nmo_object_t *nmo_inspect_find_object_by_id(nmo_object_t **objects, size_t object_count, nmo_object_id_t id);

void nmo_inspect_resolve_scene_root(inspect_state_t *state, inspect_options_t *opts);

bool nmo_inspect_resolve_class_filter(const inspect_state_t *state, inspect_options_t *opts);

void nmo_inspect_collect_stats(inspect_state_t *state);

void nmo_inspect_collect_plugin_warnings(const inspect_state_t *state, const inspect_options_t *opts, warning_list_t *warnings);

void nmo_inspect_collect_chunk_warnings(const inspect_state_t *state,
                                        const inspect_options_t *opts,
                                        warning_list_t *warnings,
                                        bool *strict_failure);

#ifdef __cplusplus
}
#endif

#endif /* NMO_INSPECT_COLLECT_H */
