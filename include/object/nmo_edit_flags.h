/**
 * @file nmo_edit_flags.h
 * @brief What an edit changed, which decides the caches it invalidates
 *
 * Workspace edits (runtime/nmo_workspace.h) report these flags, and the session
 * drops its behavior index, reference graph and query caches by them.
 */

#ifndef NMO_EDIT_FLAGS_H
#define NMO_EDIT_FLAGS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmo_workspace_edit_flags {
    NMO_WORKSPACE_EDIT_OBJECT_STATE   = 1u << 0,
    NMO_WORKSPACE_EDIT_REFERENCES     = 1u << 1,
    NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH = 1u << 2,
    NMO_WORKSPACE_EDIT_NAMES          = 1u << 3,
    NMO_WORKSPACE_EDIT_RESOURCES      = 1u << 4
} nmo_workspace_edit_flags_t;

#ifdef __cplusplus
}
#endif

#endif /* NMO_EDIT_FLAGS_H */
