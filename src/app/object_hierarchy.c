#include "app/nmo_object_hierarchy.h"

#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "format/nmo_object.h"
#include "session/nmo_ref_enumerate.h"

#include <stdlib.h>
#include <string.h>

static int classify_ownership_field(const char *field_name) {
    if (!field_name) {
        return 0;
    }

    if (strcmp(field_name, "script_ids") == 0) {
        return 1;
    }
    if (strcmp(field_name, "sub_behaviors") == 0) {
        return 1;
    }
    if (strcmp(field_name, "sub_behavior_links") == 0) {
        return 1;
    }
    if (strcmp(field_name, "operations") == 0) {
        return 1;
    }
    if (strcmp(field_name, "in_parameters") == 0) {
        return 1;
    }
    if (strcmp(field_name, "out_parameters") == 0) {
        return 1;
    }
    if (strcmp(field_name, "local_parameters") == 0) {
        return 1;
    }
    if (strcmp(field_name, "inputs") == 0) {
        return 1;
    }
    if (strcmp(field_name, "outputs") == 0) {
        return 1;
    }
    if (strcmp(field_name, "target_parameter_id") == 0) {
        return 1;
    }
    if (strcmp(field_name, "attribute_parameter_ids") == 0) {
        return 1;
    }
    if (strcmp(field_name, "in1_id") == 0) {
        return 1;
    }
    if (strcmp(field_name, "in2_id") == 0) {
        return 1;
    }
    if (strcmp(field_name, "out_id") == 0) {
        return 1;
    }

    if (strcmp(field_name, "parent_id") == 0) {
        return -1;
    }

    return 0;
}

typedef struct nmo_object_hierarchy_builder_ctx {
    nmo_object_id_t *parent_of;
    size_t map_size;
    nmo_object_id_t source_id;
} nmo_object_hierarchy_builder_ctx_t;

static bool tree_ownership_visitor(
    void *user_data,
    uint32_t target_id,
    nmo_ref_kind_t kind,
    const char *field_name,
    uint32_t index) {
    (void)kind;
    (void)index;
    nmo_object_hierarchy_builder_ctx_t *ctx = (nmo_object_hierarchy_builder_ctx_t *)user_data;

    int own = classify_ownership_field(field_name);
    if (own == 0) {
        return true;
    }

    if (own == 1) {
        if (target_id > 0 && target_id < ctx->map_size && ctx->parent_of[target_id] == 0) {
            ctx->parent_of[target_id] = ctx->source_id;
        }
    } else {
        if (target_id > 0 && target_id < ctx->map_size &&
            ctx->source_id > 0 && ctx->source_id < ctx->map_size &&
            ctx->parent_of[ctx->source_id] == 0) {
            ctx->parent_of[ctx->source_id] = target_id;
        }
    }

    return true;
}

bool nmo_object_hierarchy_build(nmo_context_t *ctx,
                                nmo_session_t *session,
                                nmo_object_hierarchy_t *out_hierarchy) {
    if (!ctx || !session || !out_hierarchy) {
        return false;
    }

    memset(out_hierarchy, 0, sizeof(*out_hierarchy));

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        return false;
    }

    nmo_object_id_t max_id = 0;
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t id = nmo_object_get_id(objects[i]);
        if (id > max_id) {
            max_id = id;
        }
    }

    size_t map_size = (size_t)max_id + 1;
    nmo_object_id_t *parent_of = (nmo_object_id_t *)calloc(map_size, sizeof(nmo_object_id_t));
    if (!parent_of) {
        return false;
    }

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_object_hierarchy_builder_ctx_t builder = {
        .parent_of = parent_of,
        .map_size = map_size,
        .source_id = 0,
    };

    for (size_t i = 0; i < object_count; ++i) {
        builder.source_id = nmo_object_get_id(objects[i]);
        nmo_ref_enumerate_object(registry, objects[i], tree_ownership_visitor, &builder);
    }

    size_t root_count = 0;
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t id = nmo_object_get_id(objects[i]);
        if (id == 0 || id >= map_size) {
            continue;
        }
        if (parent_of[id] == 0) {
            root_count++;
        }
    }

    out_hierarchy->parent_of = parent_of;
    out_hierarchy->map_size = map_size;
    out_hierarchy->root_count = root_count;
    out_hierarchy->object_count = object_count;
    return true;
}

void nmo_object_hierarchy_free(nmo_object_hierarchy_t *hierarchy) {
    if (!hierarchy) {
        return;
    }
    free(hierarchy->parent_of);
    memset(hierarchy, 0, sizeof(*hierarchy));
}
