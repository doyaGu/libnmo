/**
 * @file param.c
 * @brief Parameter values: real sources, operations, and output propagation.
 */

#include "../engine_internal.h"

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

bool eng_param_resize(eng_param_t *param, size_t size)
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

/* GetParameterGuidParentGuid: the type a parameter type derives from; NULL for none. */
static const nmo_type_descriptor_t *eng_type_parent(const nmo_engine_t *engine,
                                                    const nmo_type_descriptor_t *type)
{
    return type != NULL && !nmo_guid_is_null(type->base_type)
               ? nmo_type_registry_find_by_guid(engine->types, type->base_type)
               : NULL;
}

static nmo_guid_t eng_type_guid(const nmo_type_descriptor_t *type)
{
    return type != NULL ? type->guid : NMO_GUID(0u, 0u);
}

/* The function registered for exactly these types: the engine's own, else the registry's. */
static bool eng_operation_exact(nmo_engine_t *engine, nmo_guid_t guid,
                                const nmo_type_descriptor_t *tr, const nmo_type_descriptor_t *t1,
                                const nmo_type_descriptor_t *t2, eng_op_lookup_t *found)
{
    found->builtin = eng_operation_builtin(guid, eng_type_guid(tr), eng_type_guid(t1),
                                           eng_type_guid(t2));
    if (found->builtin != NULL) {
        return true;
    }
    const nmo_operation_tree_cell_t *cell = NULL;
    if (t1 != NULL && tr != NULL &&
        nmo_operation_registry_find_typed(engine->operations_registry, &guid, t1, t2, tr, NULL,
                                          &cell) == NMO_OK &&
        cell != NULL && cell->desc.function != NULL) {
        found->cell = cell;
        return true;
    }
    return false;
}

typedef struct eng_type_triple {
    const nmo_type_descriptor_t *tr, *t1, *t2;
} eng_type_triple_t;

/* The type triples a lookup found nothing for: a triple fails the same way each time */
typedef ENG_VEC(eng_type_triple_t) eng_type_triples_t;

/*
 * CKParameterManager::GetOperationFunction: the function registered for
 * exactly these types, else for their parents, in CK's order: all three,
 * both operands, the first operand and the result, the first operand, the
 * second and the result, the second, then the result alone.
 */
static bool eng_operation_lookup(nmo_engine_t *engine, nmo_guid_t guid,
                                 const nmo_type_descriptor_t *tr, const nmo_type_descriptor_t *t1,
                                 const nmo_type_descriptor_t *t2, eng_type_triples_t *failed,
                                 eng_op_lookup_t *found)
{
    for (size_t i = 0; i < failed->count; i++) {
        const eng_type_triple_t *known = &failed->data[i];
        if (known->tr == tr && known->t1 == t1 && known->t2 == t2) {
            return false;
        }
    }
    if (eng_operation_exact(engine, guid, tr, t1, t2, found)) {
        return true;
    }
    const nmo_type_descriptor_t *pr = eng_type_parent(engine, tr);
    const nmo_type_descriptor_t *p1 = eng_type_parent(engine, t1);
    const nmo_type_descriptor_t *p2 = eng_type_parent(engine, t2);
    const nmo_type_descriptor_t *tries[7][3] = {
        {pr, p1, p2}, {tr, p1, p2}, {pr, p1, t2}, {tr, p1, t2},
        {pr, t1, p2}, {tr, t1, p2}, {pr, t1, t2},
    };
    const bool allowed[7] = {
        p1 && p2 && pr, p1 && p2, p1 && pr, p1 != NULL, p2 && pr, p2 != NULL, pr != NULL,
    };
    for (int i = 0; i < 7; i++) {
        if (allowed[i] &&
            eng_operation_lookup(engine, guid, tries[i][0], tries[i][1], tries[i][2], failed,
                                 found)) {
            return true;
        }
    }
    eng_type_triple_t *slot = NULL;
    if (ENG_VEC_PUSH(*failed, slot)) {
        *slot = (eng_type_triple_t){tr, t1, t2};
    }
    return false;
}

/*
 * Find the function of `guid` for the operand and result types, swapping
 * the operands as the Op block does; what was found for these types is kept.
 */
static const eng_op_lookup_t *eng_operation_find(nmo_engine_t *engine, nmo_guid_t guid,
                                                 const nmo_type_descriptor_t *t1,
                                                 const nmo_type_descriptor_t *t2,
                                                 const nmo_type_descriptor_t *tr)
{
    for (size_t i = 0; i < engine->op_lookups.count; i++) {
        const eng_op_lookup_t *known = &engine->op_lookups.data[i];
        if (known->t1 == t1 && known->t2 == t2 && known->tr == tr &&
            nmo_guid_equals(known->guid, guid)) {
            return known;
        }
    }
    eng_op_lookup_t found = {guid, t1, t2, tr, NULL, NULL, false};
    eng_type_triples_t failed = {0};
    if (!eng_operation_lookup(engine, guid, tr, t1, t2, &failed, &found) && t2 != NULL) {
        found.swap = eng_operation_lookup(engine, guid, tr, t2, t1, &failed, &found);
    }
    free(failed.data);
    eng_op_lookup_t *known = NULL;
    if (!ENG_VEC_PUSH(engine->op_lookups, known)) {
        return NULL;
    }
    *known = found;
    return known;
}

/* The data a registry function reads for an operand: a source-less one reads zeros, as CK's do */
static const void *eng_operand_data(const eng_param_t *src, const nmo_type_descriptor_t *type,
                                    void **zeros)
{
    if (src != NULL || type == NULL) {
        return src != NULL ? src->data : NULL;
    }
    *zeros = calloc(1, type->size > 0 ? type->size : 1);
    return *zeros;
}

nmo_status_t eng_operation_execute(nmo_engine_t *engine, nmo_guid_t guid, eng_param_t *in1,
                                   eng_param_t *in2, eng_param_t *out, bool need_sources)
{
    if (out == NULL || out->type_desc == NULL || engine->operations_registry == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    /* The operands are the values the inputs read, brought up to date */
    eng_param_t *src1 = eng_is_none_type(in1) ? NULL : eng_param_real_source(engine, in1);
    eng_param_t *src2 = eng_is_none_type(in2) ? NULL : eng_param_real_source(engine, in2);
    if (need_sources &&
        ((!eng_is_none_type(in1) && src1 == NULL) || (!eng_is_none_type(in2) && src2 == NULL))) {
        return NMO_ERR_INVALID_STATE; /* CKERR_NOTINITIALIZED */
    }
    eng_param_update(engine, src1);
    eng_param_update(engine, src2);

    /* The function is the one for the inputs' own types, whatever they read */
    const nmo_type_descriptor_t *t1 = eng_is_none_type(in1) ? NULL : in1->type_desc;
    const nmo_type_descriptor_t *t2 = eng_is_none_type(in2) ? NULL : in2->type_desc;
    const eng_op_lookup_t *found = eng_operation_find(engine, guid, t1, t2, out->type_desc);
    eng_builtin_op_fn builtin = found != NULL ? found->builtin : NULL;
    if (found != NULL && found->swap) {
        eng_param_t *t = src1;
        src1 = src2;
        src2 = t;
        const nmo_type_descriptor_t *tt = t1;
        t1 = t2;
        t2 = tt;
    }
    if (builtin == NULL && (found == NULL || found->cell == NULL)) {
        builtin = eng_operation_builtin_text(guid, out->type, eng_type_guid(t1), eng_type_guid(t2));
    }
    if (builtin != NULL) {
        return builtin(engine, src1, src2, out);
    }
    if (found == NULL || found->cell == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_operation_tree_cell_t *cell = found->cell;
    if (out->type_desc->size > out->size && !eng_param_resize(out, out->type_desc->size)) {
        return NMO_ERR_NOMEM;
    }
    void *zeros1 = NULL, *zeros2 = NULL;
    const void *data1 = eng_operand_data(src1, t1, &zeros1);
    const void *data2 = eng_operand_data(src2, t2, &zeros2);
    nmo_status_t status = (t1 != NULL && data1 == NULL) || (t2 != NULL && data2 == NULL)
                              ? NMO_ERR_NOMEM
                              : cell->desc.function(data1, t1, data2, t2, out->data,
                                                    out->type_desc, cell->desc.user_data);
    free(zeros1);
    free(zeros2);
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
    nmo_status_t status = eng_operation_execute(engine, op->guid, in1, in2, out, true);
    op->running = false;
    eng_emit(engine, NMO_ENGINE_EVENT_OPERATION, -1, op->id, (int32_t)status);
    return status;
}
