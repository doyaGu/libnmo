#ifndef NMO_PROJECT_INTERNAL_H
#define NMO_PROJECT_INTERNAL_H

#include "core/nmo_error.h"
#include "nmo_types.h"

#include <stddef.h>

typedef struct nmo_project_plan nmo_project_plan_t;
typedef struct nmo_workspace nmo_workspace_t;
typedef struct nmo_workspace_edit nmo_workspace_edit_t;

typedef struct nmo_project_runtime_object {
    uint32_t plan_handle;
    nmo_object_id_t object_id;
} nmo_project_runtime_object_t;

/* An external texture is named after its file, folders and extension cut,
   as the game's own files name theirs; textures of the same file are one. */
void nmo_project_texture_file_stem(
    const char *path,
    const char **out_start,
    size_t *out_length);

nmo_status_t nmo_project_author_scenes(
    nmo_workspace_edit_t *edit,
    const nmo_project_plan_t *plan,
    nmo_project_runtime_object_t **out_objects,
    size_t *out_object_count);

nmo_status_t nmo_project_author_assets(
    nmo_workspace_edit_t *edit,
    const nmo_project_plan_t *plan,
    const nmo_project_runtime_object_t *objects,
    size_t object_count);

nmo_status_t nmo_project_author_groups(
    nmo_workspace_edit_t *edit,
    const nmo_project_plan_t *plan,
    const nmo_project_runtime_object_t *objects,
    size_t object_count);

nmo_status_t nmo_project_author_scripts(
    nmo_workspace_t *workspace,
    const nmo_project_plan_t *plan,
    const nmo_project_runtime_object_t *objects,
    size_t object_count);

#endif /* NMO_PROJECT_INTERNAL_H */
