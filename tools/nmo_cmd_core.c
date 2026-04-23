/**
 * @file nmo_cmd_core.c
 * @brief Shared command core - reusable logic for CLI and REPL commands
 */

#include "nmo_cmd_core.h"
#include "nmo_cli_common.h"
#include "nmo_tool_common.h"
#include "document/nmo_document.h"
#include "object/nmo_object_query.h"
#include "object/nmo_object_refs.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "runtime/nmo_workspace.h"
#include "type/nmo_type_string.h"
#include "object/nmo_object_repository.h"
#include "core/nmo_guid.h"
#include "core/nmo_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

/* ============================================================================
 * 1. Class utilities
 * ============================================================================ */

const char *nmo_core_class_name(const nmo_cmd_ctx_t *c, nmo_class_id_t id) {
    return nmo_cli_class_name_from_id(c->ctx, id);
}

const char *nmo_core_class_name_or(const nmo_cmd_ctx_t *c, nmo_class_id_t id,
                                   char *buf, size_t sz) {
    const char *name = nmo_core_class_name(c, id);
    if (name) return name;
    snprintf(buf, sz, "Class#%u", (unsigned)id);
    return buf;
}

nmo_class_id_t nmo_core_class_id(const nmo_cmd_ctx_t *c, const char *name) {
    return nmo_cli_class_id_from_name(c->ctx, name);
}

bool nmo_core_class_derives(const nmo_cmd_ctx_t *c, nmo_class_id_t id,
                            nmo_class_id_t base) {
    return nmo_cli_class_is_derived_from(c->ctx, id, base);
}

/* ============================================================================
 * 2. Object lookup
 * ============================================================================ */

nmo_object_t *nmo_core_find_by_id(const nmo_cmd_ctx_t *c, nmo_object_id_t id) {
    nmo_object_repository_t *repo = nmo_session_get_repository(c->session);
    return nmo_object_repository_find_by_id(repo, id);
}

int nmo_core_find_by_name(const nmo_cmd_ctx_t *c,
                          const char *name,
                          nmo_object_t **out_object)
{
    if (c == NULL || c->document == NULL || name == NULL || out_object == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_query_t query = {
        .name = name,
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
        .name_case_insensitive = false
    };
    nmo_status_t status = nmo_object_query_find_first(
        c->document,
        &query,
        out_object,
        NULL);
    if (status == NMO_ERR_NOT_FOUND) {
        return NMO_CLI_EXIT_NOT_FOUND;
    }
    return status == NMO_OK ? NMO_CLI_EXIT_SUCCESS : NMO_CLI_EXIT_INTERNAL_ERROR;
}

int nmo_core_resolve_one_object(
    const nmo_cmd_ctx_t *c,
    const nmo_core_object_selector_t *selector,
    nmo_object_t **out_object,
    nmo_object_id_t *out_id)
{
    if (c == NULL || selector == NULL || out_object == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    *out_object = NULL;
    if (out_id != NULL) {
        *out_id = 0;
    }

    const char *label = selector->selector_label != NULL
        ? selector->selector_label
        : "Object";
    const char *type_label = selector->type_label != NULL
        ? selector->type_label
        : label;

    nmo_object_t *obj = NULL;
    nmo_object_id_t id = 0;
    bool has_id = selector->has_id;
    if (!has_id && selector->positional_id != NULL) {
        uint32_t parsed_id = 0;
        if (!nmo_tool_parse_u32(selector->positional_id, &parsed_id) || parsed_id == 0) {
            fprintf(stderr, "Error: Invalid %s ID '%s'\n", label, selector->positional_id);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        id = (nmo_object_id_t)parsed_id;
        has_id = true;
    } else if (selector->has_id) {
        id = selector->id;
    }

    if (!has_id && (selector->name == NULL || selector->name[0] == '\0')) {
        fprintf(stderr, "Error: No %s selector specified\n", label);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_selector_t object_selector = {
        .has_id = has_id,
        .id = id,
        .name = selector->name,
        .required_base_class = selector->required_base_class,
        .allowed_class_ids = selector->allowed_class_ids,
        .allowed_class_count = selector->allowed_class_count
    };
    nmo_status_t status = c->document != NULL
        ? nmo_object_query_resolve_one(c->document, &object_selector, &obj, &id)
        : NMO_ERR_INVALID_ARGUMENT;
    if (status == NMO_ERR_NOT_FOUND) {
        if (object_selector.has_id) {
            fprintf(stderr, "Error: Object %u not found\n", id);
        } else {
            fprintf(stderr, "Error: %s '%s' not found\n", label, selector->name);
        }
        return NMO_CLI_EXIT_NOT_FOUND;
    }
    if (status != NMO_OK) {
        if (status == NMO_ERR_INVALID_ARGUMENT && obj != NULL) {
            fprintf(stderr, "Error: Object %u is not a %s (class %u)\n",
                    id, type_label, nmo_object_get_class_id(obj));
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (status == NMO_ERR_INVALID_ARGUMENT) {
            fprintf(stderr, "Error: Invalid %s selector\n", label);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    *out_object = obj;
    if (out_id != NULL) {
        *out_id = id;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * 3. Object iteration
 * ============================================================================ */

typedef struct nmo_core_object_query_bridge {
    const nmo_cmd_ctx_t *cmd;
    nmo_core_object_fn visitor;
    void *user;
} nmo_core_object_query_bridge_t;

static bool nmo_core_object_query_visit(
    size_t object_index,
    nmo_object_t *object,
    void *user_data)
{
    nmo_core_object_query_bridge_t *bridge =
        (nmo_core_object_query_bridge_t *)user_data;
    if (bridge == NULL || bridge->visitor == NULL) {
        return true;
    }
    return bridge->visitor(object_index, object, bridge->cmd, bridge->user) == 0;
}

int nmo_core_object_query_run(const nmo_cmd_ctx_t *c,
                              const nmo_object_query_t *query,
                              nmo_core_object_fn visitor,
                              void *user,
                              nmo_core_iter_result_t *result)
{
    if (c == NULL || c->document == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_core_object_query_bridge_t bridge = {
        .cmd = c,
        .visitor = visitor,
        .user = user
    };
    nmo_object_query_context_t query_ctx = {
        .repository = nmo_document_get_repository(c->document),
        .index = NULL,
        .registry = c->registry
    };
    nmo_object_query_result_t query_result = {0};
    nmo_status_t status = nmo_object_query_iterate(
        &query_ctx,
        query,
        visitor != NULL ? nmo_core_object_query_visit : NULL,
        &bridge,
        &query_result);

    if (result != NULL) {
        result->total = query_result.total;
        result->matched = query_result.matched;
        result->visited = query_result.visited;
    }

    return status == NMO_OK ? NMO_CLI_EXIT_SUCCESS : NMO_CLI_EXIT_INTERNAL_ERROR;
}

int nmo_core_object_query_first(const nmo_cmd_ctx_t *c,
                                const nmo_object_query_t *query,
                                nmo_object_t **out_object,
                                size_t *out_index)
{
    if (c == NULL || c->document == NULL || out_object == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_status_t status = nmo_object_query_find_first(
        c->document,
        query,
        out_object,
        out_index);
    if (status == NMO_ERR_NOT_FOUND) {
        return NMO_CLI_EXIT_NOT_FOUND;
    }
    return status == NMO_OK ? NMO_CLI_EXIT_SUCCESS : NMO_CLI_EXIT_INTERNAL_ERROR;
}

int nmo_core_object_count(const nmo_cmd_ctx_t *c, size_t *out_count)
{
    if (c == NULL || c->document == NULL || out_count == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_status_t status = nmo_object_query_count(c->document, NULL, out_count);
    return status == NMO_OK ? NMO_CLI_EXIT_SUCCESS : NMO_CLI_EXIT_INTERNAL_ERROR;
}

nmo_status_t nmo_core_query_set_class_name(
    const nmo_cmd_ctx_t *c,
    nmo_object_query_t *query,
    const char *class_name,
    bool include_derived)
{
    if (c == NULL || query == NULL || class_name == NULL || class_name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_class_id_t class_id = nmo_core_class_id(c, class_name);
    if (class_id == 0) {
        return NMO_ERR_NOT_FOUND;
    }

    query->class_id = class_id;
    query->include_derived_classes = include_derived;
    return NMO_OK;
}

void nmo_core_query_set_class_id(
    nmo_object_query_t *query,
    nmo_class_id_t class_id,
    bool include_derived)
{
    if (query == NULL) {
        return;
    }
    query->class_id = class_id;
    query->include_derived_classes = include_derived;
}

void nmo_core_query_set_name_wildcard(
    nmo_object_query_t *query,
    const char *pattern)
{
    if (query == NULL) {
        return;
    }
    if (pattern == NULL) {
        query->name = NULL;
        query->name_mode = NMO_OBJECT_QUERY_NAME_NONE;
        query->name_case_insensitive = false;
        return;
    }

    query->name = pattern;
    query->name_mode = NMO_OBJECT_QUERY_NAME_WILDCARD;
    query->name_case_insensitive = true;
}

void nmo_core_query_set_object_id(
    nmo_object_query_t *query,
    nmo_object_id_t object_id)
{
    if (query == NULL) {
        return;
    }
    query->object_id = object_id;
}

int nmo_core_query_build(
    const nmo_cmd_ctx_t *c,
    nmo_object_query_t *query,
    const nmo_core_query_build_options_t *opts)
{
    if (c == NULL || query == NULL || opts == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    memset(query, 0, sizeof(*query));

    if (opts->class_name != NULL) {
        if (nmo_core_query_set_class_name(
                c, query, opts->class_name,
                opts->include_derived_classes) != NMO_OK) {
            fprintf(stderr, "Error: Unknown class '%s'\n", opts->class_name);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    if (opts->name_wildcard != NULL) {
        nmo_core_query_set_name_wildcard(query, opts->name_wildcard);
    }

    if (opts->has_object_id) {
        nmo_core_query_set_object_id(query, opts->object_id);
    }

    return NMO_CLI_EXIT_SUCCESS;
}

bool nmo_core_query_matches_object(
    const nmo_cmd_ctx_t *c,
    const nmo_object_query_t *query,
    const nmo_object_t *object)
{
    if (query == NULL) {
        return true;
    }
    if (c == NULL || object == NULL) {
        return false;
    }

    bool matches = false;
    return nmo_object_query_matches(object, query, c->registry, &matches) == NMO_OK &&
           matches;
}

/* ============================================================================
 * 4. Reference iteration
 * ============================================================================ */

typedef struct nmo_core_ref_bridge {
    const nmo_cmd_ctx_t *cmd;
    nmo_core_ref_fn visitor;
    void *user;
} nmo_core_ref_bridge_t;

static bool nmo_core_visit_ref_edge(
    const nmo_object_refs_edge_t *edge,
    void *user_data)
{
    nmo_core_ref_bridge_t *state = (nmo_core_ref_bridge_t *)user_data;
    if (state == NULL || state->visitor == NULL || edge == NULL || edge->edge == NULL) {
        return true;
    }
    nmo_core_ref_info_t info = {
        .edge = edge->edge,
        .is_incoming = edge->is_incoming,
        .peer = edge->peer,
        .peer_name = edge->peer != NULL ? nmo_object_get_name(edge->peer) : NULL,
        .peer_class_name = edge->peer != NULL
            ? nmo_core_class_name(state->cmd, nmo_object_get_class_id(edge->peer))
            : NULL
    };
    return state->visitor(&info, state->cmd, state->user) == 0;
}

int nmo_core_iter_refs(const nmo_cmd_ctx_t *c,
                       nmo_object_id_t obj_id,
                       unsigned dir,
                       nmo_core_ref_fn visitor, void *user,
                       nmo_core_ref_result_t *result) {
    if (c == NULL || c->document == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_core_ref_bridge_t bridge = {
        .cmd = c,
        .visitor = visitor,
        .user = user
    };
    nmo_object_refs_result_t refs_result = {0};
    nmo_object_refs_direction_t direction = 0;
    if ((dir & NMO_CORE_REFS_OUT) != 0u) {
        direction = (nmo_object_refs_direction_t)(direction | NMO_OBJECT_REFS_OUTGOING);
    }
    if ((dir & NMO_CORE_REFS_IN) != 0u) {
        direction = (nmo_object_refs_direction_t)(direction | NMO_OBJECT_REFS_INCOMING);
    }

    nmo_status_t status = nmo_object_refs_iterate(
        c->document,
        obj_id,
        direction,
        visitor != NULL ? nmo_core_visit_ref_edge : NULL,
        &bridge,
        &refs_result);
    if (status != NMO_OK) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (result != NULL) {
        result->outgoing = refs_result.outgoing;
        result->incoming = refs_result.incoming;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * 5. Field mutation
 * ============================================================================ */

int nmo_core_set_fields(nmo_cmd_ctx_t *c, nmo_object_id_t object_id,
                        const nmo_field_set_entry_t *entries, size_t entry_count,
                        bool dry_run, nmo_field_set_result_t *out_result)
{
    nmo_field_set_result_t result = {0, 0};

    /* Resolve object -> state + type descriptor once */
    nmo_object_repository_t *repo = nmo_session_get_repository(c->session);
    if (!repo) {
        fprintf(stderr, "Error: No object repository\n");
        if (out_result) *out_result = result;
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object #%u not found\n", object_id);
        if (out_result) *out_result = result;
        return NMO_CLI_EXIT_NOT_FOUND;
    }

    void *state = nmo_object_get_state(obj);
    if (!state) {
        fprintf(stderr, "Error: Object #%u has no typed state\n", object_id);
        if (out_result) *out_result = result;
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_class_id_inherited(
            c->registry, nmo_object_get_class_id(obj));
    if (!type) {
        fprintf(stderr, "Error: No type descriptor for object #%u\n", object_id);
        if (out_result) *out_result = result;
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_workspace_edit_t *edit = NULL;
    nmo_status_t begin_rc = c->workspace != NULL
        ? nmo_workspace_edit_begin(c->workspace, "cli set fields", &edit)
        : NMO_ERR_INVALID_ARGUMENT;
    if (begin_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin edit: %s\n",
                nmo_error_string(begin_rc));
        if (out_result) *out_result = result;
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    for (size_t i = 0; i < entry_count; i++) {
        const char *fname = entries[i].field_name;
        const char *vstr  = entries[i].value_str;

        /* Read old value */
        char old_buf[256];
        old_buf[0] = '\0';
        nmo_type_get_field(state, type, c->registry, fname,
                           old_buf, sizeof(old_buf));

        nmo_session_field_edit_t field = {
            .field_name = fname,
            .value_str = vstr,
        };
        nmo_status_t rc =
            nmo_object_edit_set_fields(edit, object_id, &field, 1, NULL);
        if (rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to set '%s' = '%s': %s\n",
                    fname, vstr, nmo_error_string(rc));
            result.failed++;
            continue;
        }

        /* Read new value */
        char new_buf[256];
        new_buf[0] = '\0';
        nmo_type_get_field(state, type, c->registry, fname,
                           new_buf, sizeof(new_buf));

        /* Print change */
        fprintf(c->out, "  %s: %s -> %s%s\n",
                fname, old_buf, new_buf, dry_run ? " (dry-run)" : "");

        result.applied++;
    }

    if (dry_run || result.failed > 0) {
        nmo_workspace_edit_rollback(edit);
    } else {
        nmo_status_t commit_rc = nmo_workspace_edit_commit(edit);
        if (commit_rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to commit edit: %s\n",
                    nmo_error_string(commit_rc));
            result.failed++;
        }
    }

    if (out_result) *out_result = result;
    return result.failed > 0 ? NMO_CLI_EXIT_INTERNAL_ERROR : NMO_CLI_EXIT_SUCCESS;
}
