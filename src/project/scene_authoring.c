#include "project_internal.h"

#include "object/nmo_class_ids.h"
#include "project/nmo_project_plan.h"
#include "../runtime/runtime_internal.h"

nmo_status_t nmo_project_author_scenes(
    nmo_document_t *document,
    const nmo_project_plan_t *plan)
{
    if (!document || !plan) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "document and plan are required");
    }

    size_t scene_count = nmo_project_plan_scene_count(plan);
    for (size_t i = 0; i < scene_count; ++i) {
        nmo_project_scene_desc_t scene = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_scene(plan, i, &scene));

        nmo_object_id_t scene_id = 0;
        NMO_RETURN_IF_ERROR(nmo_document_internal_create_object(
            document,
            NMO_CID_SCENE,
            scene.name,
            (nmo_guid_t){0, 0},
            &scene_id));
    }

    NMO_RETURN_OK();
}
