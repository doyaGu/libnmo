/**
 * @file engine_schedule.c
 * @brief Graph execution and the behavior manager's frame loop.
 *
 * Follows CK2: CKBehaviorManager::Execute, CKBeObject::ExecuteBehaviors,
 * CKBehavior::Execute, CheckIOsActivation, FindNextBehaviorsToExecute,
 * CheckBehaviorActivity, Activate and Reset.
 */

#include "engine_internal.h"

#include "object/nmo_object_enum_defs.h"

#include <stdlib.h>
#include <string.h>

#define ENG_NEXT_FRAME_FLAGS                                                                \
    (CKBEHAVIOR_ACTIVATENEXTFRAME | CKBEHAVIOR_DEACTIVATENEXTFRAME | CKBEHAVIOR_RESETNEXTFRAME)

static inline eng_node_t *eng_node(nmo_engine_t *engine, int32_t index)
{
    return &engine->nodes.data[index];
}

static void eng_io_set(nmo_engine_t *engine, int32_t io, bool active)
{
    if (io >= 0) {
        engine->ios.data[io].active = active;
    }
}

/* ============================================================================
 * Activation
 * ============================================================================ */

void eng_behavior_reset(nmo_engine_t *engine, int32_t index)
{
    eng_node_t *node = eng_node(engine, index);
    if (!node->is_block) {
        for (uint32_t i = 0; i < node->link_count; i++) {
            eng_link_t *link = &engine->links.data[node->first_link + i];
            link->delay = link->initial_delay;
        }
        for (uint32_t i = 0; i < node->delayed_count; i++) {
            engine->links.data[*eng_index(engine, node->first_delayed + i)].in_delayed_list = false;
        }
        node->delayed_count = 0;
        for (uint32_t i = 0; i < node->child_count; i++) {
            int32_t child = *eng_index(engine, node->first_child + i);
            eng_node(engine, child)->flags &= ~(uint32_t)CKBEHAVIOR_ACTIVE;
            eng_behavior_reset(engine, child);
        }
    }
    for (uint32_t i = 0; i < node->input_count; i++) {
        eng_io_set(engine, *eng_index(engine, node->first_input + i), false);
    }
    for (uint32_t i = 0; i < node->output_count; i++) {
        eng_io_set(engine, *eng_index(engine, node->first_output + i), false);
    }
    node->flags |= CKBEHAVIOR_LAUNCHEDONCE;
    node->flags &= ~(uint32_t)(CKBEHAVIOR_WAITSFORMESSAGE | ENG_NEXT_FRAME_FLAGS);
    if ((node->flags & CKBEHAVIOR_SCRIPT) != 0 && node->input_count > 0) {
        eng_io_set(engine, *eng_index(engine, node->first_input), true);
    }
}

void eng_behavior_activate(nmo_engine_t *engine, int32_t index, bool active, bool reset)
{
    eng_node_t *node = eng_node(engine, index);
    if (reset || node->flags == 0) {
        eng_behavior_reset(engine, index);
    }
    if (active) {
        node->flags |= CKBEHAVIOR_ACTIVE;
    } else {
        node->flags &= ~(uint32_t)CKBEHAVIOR_ACTIVE;
    }
}

void eng_object_next_frame(nmo_engine_t *engine, int32_t object, int32_t delta)
{
    if (object >= 0) {
        engine->objects.data[object].next_frame += delta;
    }
}

/* ============================================================================
 * Building block functions
 * ============================================================================ */

int eng_block_call(nmo_engine_t *engine, int32_t node, nmo_engine_block_fn fn)
{
    nmo_engine_block_t block = {.engine = engine, .node = node};
    return fn(&block);
}

int eng_block_callback(nmo_engine_t *engine, int32_t node, uint32_t message)
{
    eng_node_t *n = eng_node(engine, node);
    if (n->block == NULL || n->block->desc.callback == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_block_t block = {.engine = engine, .node = node};
    return n->block->desc.callback(&block, message);
}

/* A building block without a function, as nmo_engine_options_t.unknown_block says. */
static int eng_execute_unknown(nmo_engine_t *engine, int32_t index)
{
    eng_node_t *node = eng_node(engine, index);
    eng_emit(engine, NMO_ENGINE_EVENT_UNKNOWN_BLOCK, index, 0, 0);
    for (uint32_t i = 0; i < node->input_count; i++) {
        eng_io_set(engine, *eng_index(engine, node->first_input + i), false);
    }
    if (engine->options.unknown_block == NMO_ENGINE_UNKNOWN_BLOCK_PASS && node->output_count > 0) {
        eng_io_set(engine, *eng_index(engine, node->first_output), true);
    }
    node->flags &= ~(uint32_t)CKBEHAVIOR_ACTIVE;
    return NMO_ENGINE_BR_OK;
}

static int eng_execute_function(nmo_engine_t *engine, int32_t index)
{
    eng_node_t *node = eng_node(engine, index);
    if (node->fn == NULL) {
        return eng_execute_unknown(engine, index);
    }
    int result = eng_block_call(engine, index, node->fn);
    node = eng_node(engine, index);
    if ((result & NMO_ENGINE_BR_ACTIVATENEXTFRAME) == 0) {
        node->flags &= ~(uint32_t)CKBEHAVIOR_ACTIVE;
    }
    return result;
}

/* ============================================================================
 * Graphs
 * ============================================================================ */

/* Put a link into its graph's delayed list, starting its countdown. */
static void eng_delay_link(nmo_engine_t *engine, eng_node_t *graph, int32_t link_index)
{
    eng_link_t *link = &engine->links.data[link_index];
    if (link->delay == 0) {
        link->delay = link->initial_delay;
    }
    if (!link->in_delayed_list && graph->delayed_count < graph->link_count) {
        link->in_delayed_list = true;
        *eng_index(engine, graph->first_delayed + graph->delayed_count++) = link_index;
        eng_emit(engine, NMO_ENGINE_EVENT_LINK_DELAYED, (int32_t)(graph - engine->nodes.data),
                 link->id, link->initial_delay);
    }
}

/*
 * CheckIOsActivation: the links whose source IO is active fire (an
 * immediate one activates its target, a delayed one starts counting), the
 * sources deactivate, then the active sub-behaviors form the iteration
 * stack with the highest priority on top.
 */
static void eng_check_ios_activation(nmo_engine_t *engine, int32_t index)
{
    eng_node_t *graph = eng_node(engine, index);
    size_t needed = (size_t)graph->link_count * 2u;
    if (!eng_vec_reserve((void **)&engine->scratch_io.data, &engine->scratch_io.capacity, needed,
                         sizeof(int32_t))) {
        needed = 0; /* out of memory: no link fires this frame */
    }
    int32_t *deactivate = engine->scratch_io.data;
    int32_t *activate = needed > 0 ? deactivate + graph->link_count : NULL;
    uint32_t deactivate_count = 0, activate_count = 0;

    for (uint32_t i = 0; needed > 0 && i < graph->link_count; i++) {
        int32_t link_index = (int32_t)(graph->first_link + i);
        eng_link_t *link = &engine->links.data[link_index];
        if (link->source < 0 || !engine->ios.data[link->source].active) {
            continue;
        }
        deactivate[deactivate_count++] = link->source;
        if (link->initial_delay != 0) {
            eng_delay_link(engine, graph, link_index);
        } else {
            activate[activate_count++] = link->target;
            eng_emit(engine, NMO_ENGINE_EVENT_LINK, index, link->id, 0);
        }
    }
    for (uint32_t i = 0; i < deactivate_count; i++) {
        eng_io_set(engine, deactivate[i], false);
    }
    for (uint32_t i = 0; i < activate_count; i++) {
        int32_t io = activate[i];
        if (io < 0) {
            continue;
        }
        engine->ios.data[io].active = true;
        if (!engine->ios.data[io].is_output && engine->ios.data[io].node >= 0) {
            eng_node(engine, engine->ios.data[io].node)->flags |= CKBEHAVIOR_ACTIVE;
        }
    }

    graph->stack_count = 0;
    for (uint32_t k = graph->child_count; k-- > 0;) {
        int32_t child = *eng_index(engine, graph->first_child + k);
        eng_node_t *sub = eng_node(engine, child);
        if (child != index && (sub->flags & CKBEHAVIOR_ACTIVE) != 0) {
            sub->in_stack = true;
            *eng_index(engine, graph->first_stack + graph->stack_count++) = child;
        }
    }
}

/* Insert a sub-behavior into the iteration stack, above those of lower or equal priority. */
static void eng_stack_insert(nmo_engine_t *engine, eng_node_t *graph, int32_t child)
{
    int32_t *stack = eng_index(engine, graph->first_stack);
    int32_t priority = eng_node(engine, child)->priority;
    int32_t pos = (int32_t)graph->stack_count - 1;
    while (pos >= 0 && priority < eng_node(engine, stack[pos])->priority) {
        pos--;
    }
    uint32_t target = (uint32_t)(pos + 1);
    for (uint32_t i = graph->stack_count; i > target; i--) {
        stack[i] = stack[i - 1];
    }
    stack[target] = child;
    graph->stack_count++;
}

/*
 * FindNextBehaviorsToExecute: the active outputs of the sub-behavior that
 * just ran deactivate and fire their links. An immediate link activates its
 * target now, so a sub-behavior it enters runs in this frame.
 */
static void eng_find_next(nmo_engine_t *engine, int32_t index, int32_t sub_index)
{
    eng_node_t *graph = eng_node(engine, index);
    eng_node_t *sub = eng_node(engine, sub_index);
    for (uint32_t o = 0; o < sub->output_count; o++) {
        int32_t out = *eng_index(engine, sub->first_output + o);
        if (out < 0 || !engine->ios.data[out].active) {
            continue;
        }
        eng_io_t *io = &engine->ios.data[out];
        io->active = false;
        for (uint32_t l = 0; l < io->link_count; l++) {
            int32_t link_index = *eng_index(engine, io->first_link + l);
            eng_link_t *link = &engine->links.data[link_index];
            if (link->initial_delay != 0) {
                eng_delay_link(engine, graph, link_index);
                continue;
            }
            if (link->target < 0) {
                continue;
            }
            eng_io_t *target = &engine->ios.data[link->target];
            target->active = true;
            eng_emit(engine, NMO_ENGINE_EVENT_LINK, index, link->id, 0);
            if (target->is_output || target->node < 0 || target->node == index) {
                continue;
            }
            eng_node_t *next = eng_node(engine, target->node);
            next->flags |= CKBEHAVIOR_ACTIVE;
            if (!next->in_stack && next->parent == index &&
                graph->stack_count < graph->child_count) {
                next->in_stack = true;
                eng_stack_insert(engine, graph, target->node);
            }
        }
    }
}

/*
 * CheckBehaviorActivity: delayed links count down and the ones that run out
 * activate their targets; the graph stays active while links wait or a
 * sub-behavior is active or waits for a message.
 */
static void eng_check_activity(nmo_engine_t *engine, int32_t index)
{
    eng_node_t *graph = eng_node(engine, index);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < graph->delayed_count; i++) {
        int32_t link_index = *eng_index(engine, graph->first_delayed + i);
        eng_link_t *link = &engine->links.data[link_index];
        if (--link->delay > 0) {
            *eng_index(engine, graph->first_delayed + kept++) = link_index;
            continue;
        }
        link->delay = 0;
        link->in_delayed_list = false;
        if (link->target >= 0) {
            eng_io_t *target = &engine->ios.data[link->target];
            if (target->node >= 0) {
                eng_node(engine, target->node)->flags |= CKBEHAVIOR_ACTIVE;
            }
            target->active = true;
        }
        eng_emit(engine, NMO_ENGINE_EVENT_LINK_ARRIVED, index, link->id, 0);
    }
    graph->delayed_count = kept;

    graph->flags &= ~(uint32_t)CKBEHAVIOR_ACTIVE;
    if (kept > 0) {
        graph->flags |= CKBEHAVIOR_ACTIVE;
        return;
    }
    for (uint32_t i = 0; i < graph->child_count; i++) {
        const eng_node_t *child = eng_node(engine, *eng_index(engine, graph->first_child + i));
        if ((child->flags & (CKBEHAVIOR_ACTIVE | CKBEHAVIOR_WAITSFORMESSAGE)) != 0) {
            graph->flags |= CKBEHAVIOR_ACTIVE;
            return;
        }
    }
}

static int eng_execute(nmo_engine_t *engine, int32_t index)
{
    eng_node_t *node = eng_node(engine, index);
    node->flags |= CKBEHAVIOR_EXECUTEDLASTFRAME;
    node->executions++;

    int result = NMO_ENGINE_BR_OK;
    if (node->is_block) {
        result = eng_execute_function(engine, index);
    } else {
        eng_check_ios_activation(engine, index);
        int32_t iterations = 0;
        while (node->stack_count > 0) {
            if (iterations > engine->options.max_iterations) {
                eng_emit(engine, NMO_ENGINE_EVENT_INFINITE_LOOP, index, 0, iterations);
                result = NMO_ENGINE_BR_INFINITELOOP;
                break;
            }
            iterations++;
            int32_t sub = *eng_index(engine, node->first_stack + --node->stack_count);
            eng_node(engine, sub)->in_stack = false;
            eng_execute(engine, sub);
            eng_find_next(engine, index, sub);
        }
        eng_check_activity(engine, index);
    }
    eng_emit(engine, NMO_ENGINE_EVENT_EXECUTE, index, 0, result);
    return result;
}

int eng_execute_behavior(nmo_engine_t *engine, int32_t node)
{
    return eng_execute(engine, node);
}

/* ============================================================================
 * Behavior manager
 * ============================================================================ */

/* CKBehaviorManager::SortObjects: higher priority first, document order otherwise. */
void eng_sort_listed(nmo_engine_t *engine)
{
    int32_t *listed = engine->listed.data;
    for (size_t k = 1; k < engine->listed.count; k++) {
        int32_t moving = listed[k];
        const eng_object_t *m = &engine->objects.data[moving];
        size_t j = k;
        while (j > 0) {
            const eng_object_t *prev = &engine->objects.data[listed[j - 1]];
            if (prev->priority > m->priority ||
                (prev->priority == m->priority && prev->order < m->order)) {
                break;
            }
            listed[j] = listed[j - 1];
            j--;
        }
        listed[j] = moving;
    }
}

/* ManageObjectsActivity: apply the additions and removals asked for last frame. */
static nmo_status_t eng_manage_objects(nmo_engine_t *engine)
{
    bool changed = false;
    for (size_t i = 0; i < engine->objects.count; i++) {
        eng_object_t *object = &engine->objects.data[i];
        if (object->next_frame < 0 && object->listed) {
            object->listed = false;
            for (size_t k = 0; k < engine->listed.count; k++) {
                if (engine->listed.data[k] == (int32_t)i) {
                    memmove(&engine->listed.data[k], &engine->listed.data[k + 1],
                            (engine->listed.count - k - 1u) * sizeof(int32_t));
                    engine->listed.count--;
                    break;
                }
            }
        } else if (object->next_frame > 0 && !object->listed) {
            int32_t *slot = NULL;
            if (!ENG_VEC_PUSH(engine->listed, slot)) {
                return NMO_ERR_NOMEM;
            }
            *slot = (int32_t)i;
            engine->objects.data[i].listed = true;
            changed = true;
        }
        engine->objects.data[i].next_frame = 0;
    }
    if (changed) {
        eng_sort_listed(engine);
    }
    return NMO_OK;
}

nmo_status_t eng_manager_execute(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->nodes.count; i++) {
        engine->nodes.data[i].flags &= ~(uint32_t)CKBEHAVIOR_EXECUTEDLASTFRAME;
    }
    nmo_status_t status = eng_manage_objects(engine);
    if (status != NMO_OK) {
        return status;
    }

    /* Scripts asked to (de)activate take the change before anything executes */
    for (size_t k = 0; k < engine->listed.count; k++) {
        const eng_object_t *object = &engine->objects.data[engine->listed.data[k]];
        for (uint32_t s = 0; s < object->script_count; s++) {
            int32_t script = *eng_index(engine, object->first_script + s);
            eng_node_t *node = eng_node(engine, script);
            if ((node->flags & (CKBEHAVIOR_ACTIVATENEXTFRAME | CKBEHAVIOR_DEACTIVATENEXTFRAME)) ==
                0) {
                continue;
            }
            bool active = (node->flags & CKBEHAVIOR_ACTIVATENEXTFRAME) != 0;
            bool reset = (node->flags & CKBEHAVIOR_RESETNEXTFRAME) != 0;
            eng_behavior_activate(engine, script, active, reset);
            eng_node(engine, script)->flags &= ~(uint32_t)ENG_NEXT_FRAME_FLAGS;
            eng_emit(engine, NMO_ENGINE_EVENT_ACTIVATE, script, 0, active ? 1 : 0);
        }
    }

    for (size_t k = 0; k < engine->listed.count; k++) {
        int32_t object_index = engine->listed.data[k];
        const eng_object_t *object = &engine->objects.data[object_index];
        bool executed = false;
        for (uint32_t s = 0; s < object->script_count; s++) {
            int32_t script = *eng_index(engine, object->first_script + s);
            if ((eng_node(engine, script)->flags & CKBEHAVIOR_ACTIVE) != 0) {
                executed = true;
                eng_execute(engine, script);
            }
        }
        if (!executed) {
            eng_object_next_frame(engine, object_index, -1);
        }
    }
    return NMO_OK;
}
