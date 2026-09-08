/**
 * @file edit_op_kind_internal.h
 * @brief Private edit operation kind metadata Interface.
 */

#ifndef NMO_BEHAVIOR_EDIT_OP_KIND_INTERNAL_H
#define NMO_BEHAVIOR_EDIT_OP_KIND_INTERNAL_H

#include "behavior/nmo_edit_plan.h"

#include <stdbool.h>

const char *nmo_edit_op_kind_result_handle(nmo_edit_op_kind_t kind);
bool nmo_edit_op_kind_creates_result(nmo_edit_op_kind_t kind);

#endif /* NMO_BEHAVIOR_EDIT_OP_KIND_INTERNAL_H */
