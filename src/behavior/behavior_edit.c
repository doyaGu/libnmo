/**
 * @file behavior_edit.c
 * @brief Behavior graph mutation: add/remove links
 */

#include "behavior/nmo_behavior_edit.h"

#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "format/nmo_object.h"
#include "core/nmo_array.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* nmo_behavior_find_parameter                                        */
/* ------------------------------------------------------------------ */

nmo_object_t *nmo_behavior_find_parameter(
    nmo_object_repository_t *repo,
    nmo_object_t *behavior,
    const char *name)
{
    if (!repo || !behavior || !name)
        return NULL;

    const nmo_behavior_state_t *bstate =
        (const nmo_behavior_state_t *)nmo_object_get_state(behavior);
    if (!bstate)
        return NULL;

    const nmo_array_t *arrays[] = {
        &bstate->in_parameters,
        &bstate->out_parameters,
        &bstate->local_parameters,
    };

    for (int a = 0; a < 3; a++) {
        const nmo_array_t *arr = arrays[a];
        if (!arr->data || arr->count == 0) continue;
        const nmo_object_id_t *ids = (const nmo_object_id_t *)arr->data;
        for (size_t i = 0; i < arr->count; i++) {
            nmo_object_t *pobj =
                nmo_object_repository_find_by_id(repo, ids[i]);
            if (!pobj) continue;
            const char *pname = nmo_object_get_name(pobj);
            if (pname && strcmp(pname, name) == 0)
                return pobj;
        }
    }
    return NULL;
}
