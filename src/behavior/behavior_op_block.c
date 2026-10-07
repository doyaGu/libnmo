/**
 * @file behavior_op_block.c
 * @brief The parameter operation an "Op" building block runs
 */

#include "behavior/nmo_behavior_analyze.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_guids.h"
#include "../runtime/runtime_internal.h"

#include <stdint.h>
#include <string.h>

/* Logics/Calculator "Op": local 1 and local 2 hold the operation GUID. */
#define NMO_BEHAVIOR_OP_BLOCK_GUID NMO_GUID(0x2D5D6D01u, 0x6A353EB0u)

static bool op_block_read_local_dword(nmo_object_repository_t *repo,
                                      const nmo_behavior_state_t *state,
                                      size_t index,
                                      uint32_t *out_value)
{
    nmo_object_id_t id = nmo_behavior_ref_array_get_id(&state->local_parameters, index);
    nmo_object_t *object = id != 0 ? nmo_object_repository_find_by_id(repo, id) : NULL;
    const nmo_parameter_state_t *param =
        object != NULL ? (const nmo_parameter_state_t *)nmo_object_get_state(object) : NULL;
    if (param == NULL || !param->has_state || param->mode != CKPARAM_MODE_BUFFER ||
        !nmo_guid_equals(param->type_guid, CKPGUID_INT) ||
        param->buffer_data.data == NULL || param->buffer_data.count < sizeof(uint32_t)) {
        return false;
    }
    memcpy(out_value, param->buffer_data.data, sizeof(*out_value));
    return true;
}

nmo_status_t nmo_behavior_op_block_operation_guid(
    nmo_workspace_t *workspace,
    nmo_object_id_t behavior_id,
    nmo_guid_t *out_guid)
{
    if (workspace == NULL || out_guid == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_guid = NMO_GUID(0u, 0u);

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(workspace);
    nmo_object_t *object = repo != NULL ? nmo_object_repository_find_by_id(repo, behavior_id) : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_object_get_state(object);
    if (state == NULL || !nmo_guid_equals(state->block_guid, NMO_BEHAVIOR_OP_BLOCK_GUID) ||
        state->local_parameters.count < 3) {
        return NMO_ERR_NOT_FOUND;
    }

    nmo_guid_t guid = NMO_GUID(0u, 0u);
    if (!op_block_read_local_dword(repo, state, 1, &guid.d1) ||
        !op_block_read_local_dword(repo, state, 2, &guid.d2) ||
        nmo_guid_is_null(guid)) {
        return NMO_ERR_NOT_FOUND;
    }
    *out_guid = guid;
    return NMO_OK;
}
