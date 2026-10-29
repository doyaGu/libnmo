/**
 * @file engine_param.c
 * @brief Parameter values: real sources, operations, and output propagation.
 */

#include "engine_internal.h"

#include "type/nmo_type_guids.h"

#include <stdlib.h>
#include <string.h>

/* Output propagation deeper than this is a cycle of destinations. */
#define ENG_MAX_PROPAGATION_DEPTH 64

eng_param_t *eng_param_real_source(nmo_engine_t *engine, eng_param_t *param)
{
    /* Shared inputs pass to the input they share; a direct source holds the value */
    for (size_t steps = 0; param != NULL && param->kind == ENG_PARAM_INPUT; steps++) {
        if (param->source < 0 || steps > engine->params.count) {
            return NULL;
        }
        param = &engine->params.data[param->source];
    }
    return param;
}

void eng_param_update(nmo_engine_t *engine, eng_param_t *param)
{
    if (param != NULL && param->operation >= 0) {
        eng_operation_run(engine, &engine->operations.data[param->operation]);
    }
}

static bool eng_types_compatible(nmo_engine_t *engine, const eng_param_t *a, const eng_param_t *b)
{
    if (nmo_guid_equals(a->type, b->type)) {
        return true;
    }
    if (a->type_desc == NULL || b->type_desc == NULL) {
        /* A type the registry does not know: allow a copy between equal sizes */
        return a->size == b->size;
    }
    return nmo_type_is_compatible(engine->types, a->type_desc->id, b->type_desc->id);
}

static bool eng_param_resize(eng_param_t *param, size_t size)
{
    if (size == param->size) {
        return true;
    }
    uint8_t *data = size > 0 ? (uint8_t *)realloc(param->data, size) : NULL;
    if (size > 0 && data == NULL) {
        return false;
    }
    if (size == 0) {
        free(param->data);
    } else if (size > param->size) {
        memset(data + param->size, 0, size - param->size);
    }
    param->data = data;
    param->size = (uint32_t)size;
    return true;
}

static nmo_status_t eng_param_copy_at(nmo_engine_t *engine, eng_param_t *dest,
                                      eng_param_t *source, int depth);

/* CKParameterOut::DataChanged: every destination takes the output's value. */
static void eng_param_data_changed(nmo_engine_t *engine, eng_param_t *param, int depth)
{
    if (param->kind != ENG_PARAM_OUTPUT || depth > ENG_MAX_PROPAGATION_DEPTH) {
        return;
    }
    for (uint32_t i = 0; i < param->destination_count; i++) {
        int32_t dest = engine->indexes.data[param->first_destination + i];
        /* Destinations never move: the params array is complete after the build */
        eng_param_copy_at(engine, &engine->params.data[dest], param, depth + 1);
    }
}

static nmo_status_t eng_param_copy_at(nmo_engine_t *engine, eng_param_t *dest,
                                      eng_param_t *source, int depth)
{
    if (dest == NULL || source == NULL || dest->kind == ENG_PARAM_INPUT) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!eng_types_compatible(engine, dest, source)) {
        return NMO_ERR_NOT_SUPPORTED; /* CKERR_INVALIDPARAMETERTYPE */
    }
    if (dest != source) {
        if (!eng_param_resize(dest, source->size)) {
            return NMO_ERR_NOMEM;
        }
        if (source->size > 0) {
            memcpy(dest->data, source->data, source->size);
        }
    }
    eng_param_data_changed(engine, dest, depth);
    return NMO_OK;
}

nmo_status_t eng_param_copy(nmo_engine_t *engine, eng_param_t *dest, eng_param_t *source,
                            bool update_source)
{
    if (update_source) {
        eng_param_update(engine, source);
    }
    return eng_param_copy_at(engine, dest, source, 0);
}

nmo_status_t eng_param_write(nmo_engine_t *engine, eng_param_t *param, const void *data,
                             size_t size)
{
    if (param == NULL || param->kind == ENG_PARAM_INPUT) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    /* CKParameter::SetValue keeps the size unless a size is given */
    if (size == 0) {
        size = param->size;
    }
    if (!eng_param_resize(param, size)) {
        return NMO_ERR_NOMEM;
    }
    if (size > 0 && data != NULL) {
        memmove(param->data, data, size);
    }
    eng_param_data_changed(engine, param, 0);
    return NMO_OK;
}

/* ============================================================================
 * Operations
 * ============================================================================ */

static bool eng_is_none_type(const eng_param_t *param)
{
    return param == NULL || nmo_guid_is_null(param->type) ||
           nmo_guid_equals(param->type, CKPGUID_NONE);
}

/* Find the function of `guid` for the operand types, swapping them as the Op block does. */
static nmo_status_t eng_operation_find(nmo_engine_t *engine, nmo_guid_t guid,
                                       const nmo_type_descriptor_t *t1,
                                       const nmo_type_descriptor_t *t2,
                                       const nmo_type_descriptor_t *tr,
                                       const nmo_operation_tree_cell_t **out_cell,
                                       bool *out_swap)
{
    *out_swap = false;
    if (nmo_operation_registry_find_typed(engine->operations_registry, &guid, t1, t2, tr,
                                          engine->types, out_cell) == NMO_OK &&
        *out_cell != NULL) {
        return NMO_OK;
    }
    if (t2 != NULL &&
        nmo_operation_registry_find_typed(engine->operations_registry, &guid, t2, t1, tr,
                                          engine->types, out_cell) == NMO_OK &&
        *out_cell != NULL) {
        *out_swap = true;
        return NMO_OK;
    }
    return NMO_ERR_NOT_FOUND;
}

nmo_status_t eng_operation_execute(nmo_engine_t *engine, nmo_guid_t guid, eng_param_t *in1,
                                   eng_param_t *in2, eng_param_t *out)
{
    if (out == NULL || out->type_desc == NULL || engine->operations_registry == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    /* The operands are the values the inputs read, brought up to date */
    eng_param_t *src1 = eng_is_none_type(in1) ? NULL : eng_param_real_source(engine, in1);
    eng_param_t *src2 = eng_is_none_type(in2) ? NULL : eng_param_real_source(engine, in2);
    if ((!eng_is_none_type(in1) && src1 == NULL) || (!eng_is_none_type(in2) && src2 == NULL)) {
        return NMO_ERR_INVALID_STATE; /* CKERR_NOTINITIALIZED */
    }
    eng_param_update(engine, src1);
    eng_param_update(engine, src2);

    nmo_status_t builtin_status = NMO_OK;
    if (eng_operation_builtin(engine, guid, in1, src1, in2, src2, out, &builtin_status)) {
        return builtin_status;
    }

    const nmo_type_descriptor_t *t1 = src1 != NULL ? in1->type_desc : NULL;
    const nmo_type_descriptor_t *t2 = src2 != NULL ? in2->type_desc : NULL;
    const nmo_operation_tree_cell_t *cell = NULL;
    bool swap = false;
    nmo_status_t status = eng_operation_find(engine, guid, t1, t2, out->type_desc, &cell, &swap);
    if (status != NMO_OK || cell->desc.function == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (out->type_desc->size > out->size && !eng_param_resize(out, out->type_desc->size)) {
        return NMO_ERR_NOMEM;
    }
    if (swap) {
        eng_param_t *t = src1;
        src1 = src2;
        src2 = t;
        const nmo_type_descriptor_t *tt = t1;
        t1 = t2;
        t2 = tt;
    }
    status = cell->desc.function(src1 != NULL ? src1->data : NULL, t1,
                                 src2 != NULL ? src2->data : NULL, t2,
                                 out->data, out->type_desc, cell->desc.user_data);
    if (status == NMO_OK) {
        eng_param_data_changed(engine, out, 0);
    }
    return status;
}

nmo_status_t eng_operation_run(nmo_engine_t *engine, eng_operation_t *op)
{
    if (op->running) {
        return NMO_ERR_INVALID_STATE;
    }
    op->running = true;
    eng_param_t *in1 = op->in1 >= 0 ? &engine->params.data[op->in1] : NULL;
    eng_param_t *in2 = op->in2 >= 0 ? &engine->params.data[op->in2] : NULL;
    eng_param_t *out = op->out >= 0 ? &engine->params.data[op->out] : NULL;
    nmo_status_t status = eng_operation_execute(engine, op->guid, in1, in2, out);
    op->running = false;
    eng_emit(engine, NMO_ENGINE_EVENT_OPERATION, -1, op->id, (int32_t)status);
    return status;
}
