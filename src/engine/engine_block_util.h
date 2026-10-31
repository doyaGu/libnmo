/**
 * @file engine_block_util.h
 * @brief Shorthands the building block ports share.
 *
 * The block files read their inputs and set their IOs through these: the
 * engine's block API (nmo_engine_block.h) with a fallback value for an input
 * that cannot be read, as a block's local variable keeps its initial value
 * when GetInputParameterValue fails.
 */

#ifndef NMO_ENGINE_BLOCK_UTIL_H
#define NMO_ENGINE_BLOCK_UTIL_H

#include "engine_internal.h"

typedef nmo_engine_block_t blk_t;

#define IN_ACTIVE(b, i) nmo_engine_block_is_input_active((b), (i))
#define IN_OFF(b, i) nmo_engine_block_activate_input((b), (i), false)
#define OUT_ON(b, i) nmo_engine_block_activate_output((b), (i), true)

static inline int32_t get_in_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
}

static inline float get_in_float(blk_t *b, int pos, float fallback)
{
    float value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
}

static inline eng_vector_t get_in_vector(blk_t *b, int pos, float x, float y, float z)
{
    eng_vector_t v = {x, y, z};
    nmo_engine_block_get_input_value(b, pos, &v, sizeof(v));
    return v;
}

static inline int32_t get_local_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_local_value(b, pos, &value, sizeof(value));
    return value;
}

static inline float get_local_float(blk_t *b, int pos, float fallback)
{
    float value = fallback;
    nmo_engine_block_get_local_value(b, pos, &value, sizeof(value));
    return value;
}

static inline void set_local_int(blk_t *b, int pos, int32_t value)
{
    nmo_engine_block_set_local_value(b, pos, &value, sizeof(value));
}

static inline int32_t get_out_int(blk_t *b, int pos)
{
    int32_t value = 0;
    nmo_engine_block_get_output_value(b, pos, &value, sizeof(value));
    return value;
}

static inline float get_out_float(blk_t *b, int pos)
{
    float value = 0.0f;
    nmo_engine_block_get_output_value(b, pos, &value, sizeof(value));
    return value;
}

static inline void set_out_int(blk_t *b, int pos, int32_t value)
{
    nmo_engine_block_set_output_value(b, pos, &value, sizeof(value));
}

static inline void set_out_float(blk_t *b, int pos, float value)
{
    nmo_engine_block_set_output_value(b, pos, &value, sizeof(value));
}

/* A string input's text; NULL when it has none or it is not terminated */
static inline const char *input_string(blk_t *b, int pos)
{
    size_t size = 0;
    const char *text = (const char *)nmo_engine_block_input_data(b, pos, &size);
    return text != NULL && size > 0 && memchr(text, '\0', size) != NULL ? text : NULL;
}

static inline void set_out(blk_t *b, int pos, const void *value, size_t size)
{
    nmo_engine_block_set_output_value(b, pos, value, size);
}

/* CKParameterManager::IsDerivedFrom */
static inline bool type_derives(blk_t *b, nmo_guid_t guid, nmo_guid_t base)
{
    if (nmo_guid_equals(guid, base)) {
        return true;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_guid(engine->types, guid);
    const nmo_type_descriptor_t *p = nmo_type_registry_find_by_guid(engine->types, base);
    return t != NULL && p != NULL && nmo_type_get_derivation_depth(engine->types, t->id, p->id) > 0;
}


/* The engine's entity the target is; -1 when it is no 3D entity. */
static inline int32_t target_entity(blk_t *b)
{
    return eng_entity_index(b->engine, nmo_engine_block_target(b));
}


/* The entity an input names; -1 for none. */
static inline int32_t input_entity(blk_t *b, int pos)
{
    return eng_entity_index(b->engine, nmo_engine_block_get_input_object(b, pos));
}


/* Hierarchy (TRUE by default) as CK passes it: KeepChildren is its opposite. */
static inline bool keep_children(blk_t *b, int pos)
{
    return get_in_int(b, pos, 1) == 0;
}


static inline const eng_node_t *block_node(const blk_t *b)
{
    return &b->engine->nodes.data[b->node];
}


/* The parameter input `pos` reads, its operation run when `update` (GetValue with update). */
static inline eng_param_t *input_source(blk_t *b, int pos, bool update)
{
    const eng_node_t *node = block_node(b);
    eng_param_t *source = eng_param_real_source(
        b->engine, eng_node_param(b->engine, node->first_pin, node->pin_count, pos));
    if (update) {
        eng_param_update(b->engine, source);
    }
    return source;
}

/* The block's output parameter at `pos`, as the engine holds it */
static inline eng_param_t *block_output(blk_t *b, int pos)
{
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    return eng_node_param(b->engine, node->first_pout, node->pout_count, pos);
}

/* Register a file's table of blocks */
static inline nmo_status_t eng_register_blocks(nmo_engine_t *engine,
                                               const nmo_engine_block_desc_t *blocks, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        nmo_status_t status = nmo_engine_register_block(engine, &blocks[i]);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}
#define ENG_REGISTER_BLOCKS(engine, table)                                                  \
    eng_register_blocks((engine), (table), sizeof(table) / sizeof((table)[0]))

#endif /* NMO_ENGINE_BLOCK_UTIL_H */
