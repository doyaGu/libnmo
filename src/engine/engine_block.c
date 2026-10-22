/**
 * @file engine_block.c
 * @brief Building block registry and the functions a block calls.
 */

#include "engine_internal.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Registry
 * ============================================================================ */

static eng_block_entry_t *eng_find_block(const nmo_engine_t *engine, nmo_guid_t guid)
{
    for (size_t i = 0; i < engine->blocks.count; i++) {
        if (nmo_guid_equals(engine->blocks.data[i].desc.guid, guid)) {
            return &engine->blocks.data[i];
        }
    }
    return NULL;
}

nmo_status_t nmo_engine_register_block(nmo_engine_t *engine, const nmo_engine_block_desc_t *desc)
{
    if (engine == NULL || desc == NULL || desc->function == NULL ||
        nmo_guid_is_null(desc->guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (engine->started) {
        return NMO_ERR_INVALID_STATE;
    }
    char *name = NULL;
    if (desc->name != NULL) {
        size_t length = strlen(desc->name);
        name = (char *)malloc(length + 1u);
        if (name == NULL) {
            return NMO_ERR_NOMEM;
        }
        memcpy(name, desc->name, length + 1u);
    }
    eng_block_entry_t *entry = eng_find_block(engine, desc->guid);
    if (entry == NULL && !ENG_VEC_PUSH(engine->blocks, entry)) {
        free(name);
        return NMO_ERR_NOMEM;
    }
    free(entry->name);
    entry->desc = *desc;
    entry->name = name;
    entry->desc.name = name;
    return NMO_OK;
}

bool nmo_engine_has_block(const nmo_engine_t *engine, nmo_guid_t guid)
{
    return engine != NULL && eng_find_block(engine, guid) != NULL;
}

/* Give every building block the function registered for its prototype. */
void eng_bind_blocks(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->nodes.count; i++) {
        eng_node_t *node = &engine->nodes.data[i];
        if (!node->is_block) {
            continue;
        }
        node->block = eng_find_block(engine, node->prototype);
        node->fn = node->block != NULL ? node->block->desc.function : NULL;
    }
}

/* ============================================================================
 * The block being executed
 * ============================================================================ */

static inline eng_node_t *eng_block_node(const nmo_engine_block_t *block)
{
    return &block->engine->nodes.data[block->node];
}

nmo_engine_t *nmo_engine_block_engine(const nmo_engine_block_t *block)
{
    return block->engine;
}

nmo_object_id_t nmo_engine_block_id(const nmo_engine_block_t *block)
{
    return eng_block_node(block)->id;
}

nmo_object_id_t nmo_engine_block_owner(const nmo_engine_block_t *block)
{
    return eng_block_node(block)->owner;
}

uint32_t nmo_engine_block_version(const nmo_engine_block_t *block)
{
    return eng_block_node(block)->version;
}

float nmo_engine_block_delta_time(const nmo_engine_block_t *block)
{
    return block->engine->delta;
}

void *nmo_engine_block_user_data(const nmo_engine_block_t *block)
{
    const eng_node_t *node = eng_block_node(block);
    return node->block != NULL ? node->block->desc.user_data : NULL;
}

void nmo_engine_block_set_function(nmo_engine_block_t *block, nmo_engine_block_fn fn)
{
    eng_block_node(block)->fn = fn;
}

double nmo_engine_block_random(nmo_engine_block_t *block)
{
    /* xorshift64* */
    uint64_t x = block->engine->random_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    block->engine->random_state = x;
    return (double)((x * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0);
}

/* ---- IOs ---- */

int nmo_engine_block_input_count(const nmo_engine_block_t *block)
{
    return (int)eng_block_node(block)->input_count;
}

int nmo_engine_block_output_count(const nmo_engine_block_t *block)
{
    return (int)eng_block_node(block)->output_count;
}

bool nmo_engine_block_is_input_active(const nmo_engine_block_t *block, int pos)
{
    const eng_io_t *io = eng_node_io(block->engine, eng_block_node(block), false, pos);
    return io != NULL && io->active;
}

bool nmo_engine_block_is_output_active(const nmo_engine_block_t *block, int pos)
{
    const eng_io_t *io = eng_node_io(block->engine, eng_block_node(block), true, pos);
    return io != NULL && io->active;
}

void nmo_engine_block_activate_input(nmo_engine_block_t *block, int pos, bool active)
{
    eng_io_t *io = eng_node_io(block->engine, eng_block_node(block), false, pos);
    if (io != NULL) {
        io->active = active;
    }
}

void nmo_engine_block_activate_output(nmo_engine_block_t *block, int pos, bool active)
{
    eng_io_t *io = eng_node_io(block->engine, eng_block_node(block), true, pos);
    if (io != NULL) {
        io->active = active;
    }
}

/* ---- Parameters ---- */

static eng_param_t *eng_block_pin(const nmo_engine_block_t *block, int pos)
{
    const eng_node_t *node = eng_block_node(block);
    return eng_node_param(block->engine, node->first_pin, node->pin_count, pos);
}

static eng_param_t *eng_block_pout(const nmo_engine_block_t *block, int pos)
{
    const eng_node_t *node = eng_block_node(block);
    return eng_node_param(block->engine, node->first_pout, node->pout_count, pos);
}

static eng_param_t *eng_block_local(const nmo_engine_block_t *block, int pos)
{
    const eng_node_t *node = eng_block_node(block);
    return eng_node_param(block->engine, node->first_local, node->local_count, pos);
}

int nmo_engine_block_input_parameter_count(const nmo_engine_block_t *block)
{
    return (int)eng_block_node(block)->pin_count;
}

int nmo_engine_block_output_parameter_count(const nmo_engine_block_t *block)
{
    return (int)eng_block_node(block)->pout_count;
}

int nmo_engine_block_local_parameter_count(const nmo_engine_block_t *block)
{
    return (int)eng_block_node(block)->local_count;
}

nmo_guid_t nmo_engine_block_input_parameter_type(const nmo_engine_block_t *block, int pos)
{
    const eng_param_t *param = eng_block_pin(block, pos);
    return param != NULL ? param->type : NMO_GUID(0u, 0u);
}

nmo_guid_t nmo_engine_block_output_parameter_type(const nmo_engine_block_t *block, int pos)
{
    const eng_param_t *param = eng_block_pout(block, pos);
    return param != NULL ? param->type : NMO_GUID(0u, 0u);
}

/* The parameter input `pos` reads, its operation run (GetRealSource + GetValue). */
static eng_param_t *eng_block_read_source(nmo_engine_block_t *block, int pos)
{
    eng_param_t *source = eng_param_real_source(block->engine, eng_block_pin(block, pos));
    eng_param_update(block->engine, source);
    return source;
}

static nmo_status_t eng_copy_out(const eng_param_t *param, void *buffer, size_t size)
{
    if (param == NULL || buffer == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    size_t n = param->size < size ? param->size : size;
    if (n > 0) {
        memcpy(buffer, param->data, n);
    }
    return NMO_OK;
}

nmo_status_t nmo_engine_block_get_input_value(nmo_engine_block_t *block, int pos, void *buffer,
                                              size_t size)
{
    return eng_copy_out(eng_block_read_source(block, pos), buffer, size);
}

const void *nmo_engine_block_input_data(nmo_engine_block_t *block, int pos, size_t *out_size)
{
    eng_param_t *source = eng_block_read_source(block, pos);
    if (out_size != NULL) {
        *out_size = source != NULL ? source->size : 0u;
    }
    return source != NULL ? source->data : NULL;
}

nmo_status_t nmo_engine_block_get_output_value(const nmo_engine_block_t *block, int pos,
                                               void *buffer, size_t size)
{
    return eng_copy_out(eng_block_pout(block, pos), buffer, size);
}

nmo_status_t nmo_engine_block_set_output_value(nmo_engine_block_t *block, int pos,
                                               const void *data, size_t size)
{
    eng_param_t *param = eng_block_pout(block, pos);
    return param != NULL ? eng_param_write(block->engine, param, data, size) : NMO_ERR_NOT_FOUND;
}

nmo_status_t nmo_engine_block_copy_input_to_output(nmo_engine_block_t *block, int in_pos,
                                                   int out_pos)
{
    eng_param_t *dest = eng_block_pout(block, out_pos);
    eng_param_t *source = eng_param_real_source(block->engine, eng_block_pin(block, in_pos));
    if (dest == NULL || source == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    return eng_param_copy(block->engine, dest, source, true);
}

nmo_status_t nmo_engine_block_get_local_value(const nmo_engine_block_t *block, int pos,
                                              void *buffer, size_t size)
{
    return eng_copy_out(eng_block_local(block, pos), buffer, size);
}

nmo_status_t nmo_engine_block_set_local_value(nmo_engine_block_t *block, int pos,
                                              const void *data, size_t size)
{
    eng_param_t *param = eng_block_local(block, pos);
    return param != NULL ? eng_param_write(block->engine, param, data, size) : NMO_ERR_NOT_FOUND;
}

/* ---- Engine-internal helpers for the built-in blocks ---- */

nmo_guid_t eng_block_operation_guid(const nmo_engine_block_t *block)
{
    return eng_block_node(block)->op_guid;
}

nmo_status_t eng_block_run_operation(nmo_engine_block_t *block, nmo_guid_t guid)
{
    return eng_operation_execute(block->engine, guid, eng_block_pin(block, 0),
                                 eng_block_pin(block, 1), eng_block_pout(block, 0));
}
