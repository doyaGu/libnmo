/**
 * @file nmo_behavior_edit.h
 * @brief Behavior graph mutation APIs
 */
#ifndef NMO_BEHAVIOR_EDIT_H
#define NMO_BEHAVIOR_EDIT_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;

/**
 * @brief Create a behavior graph link.
 *
 * Creates a CKBehaviorLink object connecting two IO ports, and appends
 * the link ID to the parent behavior's sub_behavior_links array.
 *
 * @param session              Session (for object creation and repository)
 * @param parent_behavior_id   Parent behavior (graph container)
 * @param from_io_id           Source IO port (output of source behavior)
 * @param to_io_id             Target IO port (input of target behavior)
 * @param activation_delay     Frames to wait before activating (typically 1)
 * @param out_link_id          Output: created link object ID (may be NULL)
 * @return NMO_OK on success
 */
NMO_API int nmo_behavior_add_link(
    nmo_session_t *session,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    int16_t activation_delay,
    nmo_object_id_t *out_link_id);

/**
 * @brief Remove a behavior graph link.
 *
 * Removes the link ID from the parent behavior's sub_behavior_links array
 * and destroys the CKBehaviorLink object.
 *
 * @param session              Session (for object destruction)
 * @param parent_behavior_id   Parent behavior
 * @param link_id              Link object to remove
 * @return NMO_OK on success
 */
NMO_API int nmo_behavior_remove_link(
    nmo_session_t *session,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_EDIT_H */
