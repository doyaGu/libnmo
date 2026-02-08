#include "dsl/nmo_dsl.h"
#include "dsl/nmo_dsl_ast.h"
#include "dsl/nmo_dsl_parse.h"
#include "nmo_dsl_eval.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_dynamic_types.h"
#include "type/nmo_type_system.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Program structure
 * ============================================================================ */

struct nmo_dsl_program {
    nmo_arena_t *arena;
    nmo_dsl_mode_t mode;
    const char *source;         /* arena copy */
    nmo_dsl_expr_t *expr;       /* EXPRESSION mode: root AST */
    nmo_dsl_stmt_t *stmts;     /* SCRIPT mode (Phase B) */
    nmo_dsl_stmt_t *schema_decls; /* SCHEMA mode (Phase C) */
};

/* ============================================================================
 * Compile
 * ============================================================================ */

static nmo_status_t compile_parse_error(const nmo_dsl_parser_t *ps, const char *fallback) {
    const char *msg = fallback;
    if (ps && ps->lx.err[0]) {
        msg = ps->lx.err;
    }
    NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "%s", msg);
}

static nmo_status_t compile_expect_eof(const nmo_dsl_parser_t *ps) {
    if (!nmo_dsl_parser_at_eof(ps)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "trailing tokens");
    }
    NMO_RETURN_OK();
}

static nmo_status_t compile_parse_expression_mode(
    nmo_dsl_program_t *prog,
    nmo_dsl_parser_t *ps)
{
    nmo_dsl_expr_t *expr = nmo_dsl_parse_expression(ps);
    if (!expr) {
        return compile_parse_error(ps, "parse error");
    }
    nmo_status_t st = compile_expect_eof(ps);
    if (st != NMO_OK) return st;
    prog->expr = expr;
    NMO_RETURN_OK();
}

static nmo_status_t compile_parse_statement_mode(
    nmo_dsl_program_t *prog,
    nmo_dsl_parser_t *ps,
    bool schema_mode)
{
    nmo_dsl_stmt_t *stmts = schema_mode ? nmo_dsl_parse_schema(ps) : nmo_dsl_parse_script(ps);
    if (!stmts && ps->lx.err[0]) {
        return compile_parse_error(ps, "parse error");
    }
    nmo_status_t st = compile_expect_eof(ps);
    if (st != NMO_OK) return st;

    if (schema_mode) {
        prog->schema_decls = stmts;
    } else {
        prog->stmts = stmts;
    }
    NMO_RETURN_OK();
}

static nmo_status_t compile_parse_module_mode(
    nmo_dsl_program_t *prog,
    nmo_dsl_parser_t *ps)
{
    nmo_dsl_module_ast_t mod = {0};
    if (!nmo_dsl_parse_module(ps, &mod)) {
        return compile_parse_error(ps, "parse error");
    }
    nmo_status_t st = compile_expect_eof(ps);
    if (st != NMO_OK) return st;

    prog->schema_decls = mod.schema_decls;
    prog->stmts = mod.script_stmts;
    NMO_RETURN_OK();
}

static nmo_status_t compile_program_ast(nmo_dsl_program_t *prog, nmo_dsl_parser_t *ps) {
    if (!prog || !ps) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "invalid args");
    }

    switch (prog->mode) {
        case NMO_DSL_MODE_EXPRESSION:
            return compile_parse_expression_mode(prog, ps);
        case NMO_DSL_MODE_SCRIPT:
            return compile_parse_statement_mode(prog, ps, false);
        case NMO_DSL_MODE_SCHEMA:
            return compile_parse_statement_mode(prog, ps, true);
        case NMO_DSL_MODE_MODULE:
            return compile_parse_module_mode(prog, ps);
        default:
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "unknown DSL mode");
    }
}

nmo_status_t nmo_dsl_compile(
    const nmo_type_registry_t *registry,
    const nmo_operation_registry_t *ops,
    const char *source,
    const nmo_dsl_compile_options_t *options,
    nmo_dsl_program_t **out_program)
{
    (void)registry;
    (void)ops;

    if (!source || !options || !out_program) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "invalid args");
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    if (!arena) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "oom");
    }

    nmo_dsl_program_t *prog = (nmo_dsl_program_t *)nmo_arena_alloc(arena, sizeof(*prog), sizeof(void *));
    if (!prog) {
        nmo_arena_destroy(arena);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "oom");
    }
    memset(prog, 0, sizeof(*prog));
    prog->arena = arena;
    prog->mode = options->mode;
    prog->source = nmo_arena_strdup(arena, source);
    if (!prog->source) {
        nmo_arena_destroy(arena);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "oom");
    }

    nmo_dsl_parser_t ps;
    nmo_dsl_parser_init(&ps, prog->source, arena);

    nmo_status_t parse_status = compile_program_ast(prog, &ps);
    if (parse_status != NMO_OK) {
        nmo_arena_destroy(arena);
        return parse_status;
    }

    *out_program = prog;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Destroy
 * ============================================================================ */

void nmo_dsl_program_destroy(nmo_dsl_program_t *program) {
    if (!program) return;
    nmo_arena_t *arena = program->arena;
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Eval expression
 * ============================================================================ */

nmo_status_t nmo_dsl_eval_expr(
    const nmo_dsl_program_t *program,
    const nmo_dsl_eval_context_t *ctx,
    nmo_dsl_value_t *out_value)
{
    if (!program || !ctx || !out_value) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "invalid args");
    }

    if (program->mode != NMO_DSL_MODE_EXPRESSION) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "program not compiled in expression mode");
    }

    nmo_dsl_eval_state_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.ctx = ctx;

    memset(out_value, 0, sizeof(*out_value));

    if (!nmo_dsl_eval_expr_impl(&ev, program->expr, out_value)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "%s", ev.err[0] ? ev.err : "eval error");
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Script/module execution
 * ============================================================================ */

nmo_status_t nmo_dsl_exec(
    const nmo_dsl_program_t *program,
    const nmo_dsl_eval_context_t *ctx,
    nmo_dsl_value_t *out_last_value)
{
    if (!program || !ctx) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "invalid args");
    }

    if (program->mode != NMO_DSL_MODE_SCRIPT && program->mode != NMO_DSL_MODE_MODULE) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "program not compiled in script/module mode");
    }

    nmo_dsl_eval_state_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.ctx = ctx;

    if (out_last_value) {
        memset(out_last_value, 0, sizeof(*out_last_value));
    }

    if (!program->stmts) {
        NMO_RETURN_OK();
    }

    if (!nmo_dsl_eval_stmt_list(&ev, program->stmts, out_last_value)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "%s", ev.err[0] ? ev.err : "exec error");
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Apply schema declarations to the type registry (Phase C)
 * ============================================================================ */

typedef struct schema_apply_context {
    nmo_type_registry_t *registry;
    const nmo_dsl_schema_options_t *options;
} schema_apply_context_t;

static nmo_guid_t schema_generate_guid(
    const schema_apply_context_t *ctx,
    const char *type_name)
{
    if (ctx && ctx->options && ctx->options->generate_guid) {
        return ctx->options->generate_guid(type_name, ctx->options->generate_guid_user);
    }
    return nmo_type_generate_guid(type_name);
}

static bool schema_next_flag_mask(uint64_t value, uint64_t *out_next) {
    if (!out_next) return false;
    if (value == 0) {
        *out_next = 1;
        return true;
    }

    uint64_t m = 1;
    while (m <= value) {
        if (m > (UINT64_MAX >> 1)) return false;
        m <<= 1;
    }
    *out_next = m;
    return true;
}

static nmo_status_t schema_validate_underlying_type(
    nmo_type_registry_t *registry,
    const char *type_name,
    nmo_guid_t expected_guid,
    const char *kind_name,
    const char *expected_name)
{
    if (!registry || !type_name || type_name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "missing underlying type");
    }

    nmo_type_parse_result_t parsed = {0};
    nmo_status_t st = nmo_type_registry_parse_type_name(registry, type_name, &parsed);
    if (st != NMO_OK) return st;

    if (parsed.is_array || parsed.is_pointer) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "underlying type must be a scalar type");
    }

    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_guid(registry, parsed.base_type_guid);
    if (!type) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "underlying type '%s' not found", type_name);
    }
    if (!(type->category & NMO_TYPE_CATEGORY_SCALAR)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "underlying type '%s' must be scalar", type_name);
    }

    if (!nmo_guid_equals(parsed.base_type_guid, expected_guid)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "%s underlying type must resolve to %s", kind_name, expected_name);
    }

    NMO_RETURN_OK();
}

static bool schema_should_skip_redeclare(
    const schema_apply_context_t *ctx,
    const char *type_name)
{
    if (!ctx || !ctx->registry || !type_name) return false;
    if (!(ctx->options && ctx->options->allow_redeclare)) return false;
    return nmo_type_registry_find_by_name(ctx->registry, type_name) != NULL;
}

static nmo_status_t schema_build_enum_values(
    const nmo_dsl_enum_decl_t *decl,
    nmo_enum_value_def_t **out_values,
    int64_t *out_default_value)
{
    if (!decl || !out_values || !out_default_value) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "invalid args");
    }

    *out_values = NULL;
    *out_default_value = 0;
    if (decl->entry_count == 0) {
        NMO_RETURN_OK();
    }

    nmo_enum_value_def_t *values =
        (nmo_enum_value_def_t *)calloc(decl->entry_count, sizeof(*values));
    if (!values) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "oom");
    }

    int64_t next_value = 0;
    bool can_autoincrement = true;
    for (size_t i = 0; i < decl->entry_count; i++) {
        int64_t value = 0;
        if (decl->entries[i].has_value) {
            value = decl->entries[i].value;
            if (value == INT64_MAX) {
                can_autoincrement = false;
            } else {
                next_value = value + 1;
                can_autoincrement = true;
            }
        } else {
            if (!can_autoincrement) {
                free(values);
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "enum value overflow");
            }
            value = next_value;
            if (next_value == INT64_MAX) {
                can_autoincrement = false;
            } else {
                next_value++;
            }
        }

        values[i].name = decl->entries[i].name;
        values[i].value = value;
        values[i].description = NULL;
    }

    *out_default_value = values[0].value;
    *out_values = values;
    NMO_RETURN_OK();
}

static nmo_status_t schema_build_flags_bits(
    const nmo_dsl_flags_decl_t *decl,
    nmo_flags_bit_def_t **out_bits)
{
    if (!decl || !out_bits) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "invalid args");
    }

    *out_bits = NULL;
    if (decl->entry_count == 0) {
        NMO_RETURN_OK();
    }

    nmo_flags_bit_def_t *bits =
        (nmo_flags_bit_def_t *)calloc(decl->entry_count, sizeof(*bits));
    if (!bits) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "oom");
    }

    uint64_t next_mask = 1;
    for (size_t i = 0; i < decl->entry_count; i++) {
        uint64_t value = 0;
        if (decl->entries[i].has_value) {
            value = decl->entries[i].value;
            if (!schema_next_flag_mask(value, &next_mask)) {
                next_mask = 0;
            }
        } else {
            if (next_mask == 0) {
                free(bits);
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "flags value overflow");
            }
            value = next_mask;
            next_mask = (next_mask <= (UINT64_MAX >> 1)) ? (next_mask << 1) : 0;
        }

        bits[i].name = decl->entries[i].name;
        bits[i].mask = value;
        bits[i].description = NULL;
    }

    *out_bits = bits;
    NMO_RETURN_OK();
}

static nmo_status_t schema_build_struct_fields(
    const nmo_dsl_struct_decl_t *decl,
    nmo_struct_field_def_t **out_fields)
{
    if (!decl || !out_fields) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "invalid args");
    }

    *out_fields = NULL;
    if (decl->field_count == 0) {
        NMO_RETURN_OK();
    }

    nmo_struct_field_def_t *fields =
        (nmo_struct_field_def_t *)calloc(decl->field_count, sizeof(*fields));
    if (!fields) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "oom");
    }

    for (size_t i = 0; i < decl->field_count; i++) {
        const char *type_name = decl->fields[i].type_name;
        if (decl->fields[i].is_repeated) {
            /* DSL "T field[]" maps to pointer-backed repeated storage. */
            size_t base_len = strlen(type_name);
            char *repeated_type = (char *)malloc(base_len + 2); /* '*' + '\0' */
            if (!repeated_type) {
                for (size_t j = 0; j < i; j++) {
                    if (fields[j].type_name &&
                        fields[j].type_name != decl->fields[j].type_name) {
                        free((void *)fields[j].type_name);
                        fields[j].type_name = NULL;
                    }
                }
                free(fields);
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "oom");
            }
            memcpy(repeated_type, type_name, base_len);
            repeated_type[base_len] = '*';
            repeated_type[base_len + 1] = '\0';
            type_name = repeated_type;
        }

        fields[i].name = decl->fields[i].field_name;
        fields[i].type_name = type_name;
        fields[i].type_guid = (nmo_guid_t){0, 0};
        fields[i].description = NULL;
        fields[i].flags = decl->fields[i].is_repeated ? NMO_FIELD_REPEATED : 0;
        fields[i].default_value = NULL;
    }

    *out_fields = fields;
    NMO_RETURN_OK();
}

static void schema_free_struct_fields(
    const nmo_dsl_struct_decl_t *decl,
    nmo_struct_field_def_t *fields)
{
    if (!decl || !fields) return;
    for (size_t i = 0; i < decl->field_count; i++) {
        if (fields[i].type_name &&
            fields[i].type_name != decl->fields[i].type_name) {
            free((void *)fields[i].type_name);
            fields[i].type_name = NULL;
        }
    }
    free(fields);
}

static nmo_status_t schema_resolve_struct_base(
    nmo_type_registry_t *registry,
    const char *base_name,
    nmo_guid_t *out_guid)
{
    if (!out_guid) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "invalid args");
    }

    *out_guid = NMO_GUID_NULL;
    if (!base_name || base_name[0] == '\0') {
        NMO_RETURN_OK();
    }

    const nmo_type_descriptor_t *base = nmo_type_registry_find_by_name(registry, base_name);
    if (!base) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "base type '%s' not found", base_name);
    }
    if (base->category != NMO_TYPE_CATEGORY_STRUCT) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "base type '%s' is not a struct", base_name);
    }
    *out_guid = base->guid;
    NMO_RETURN_OK();
}

static nmo_status_t schema_apply_enum_decl(
    const schema_apply_context_t *ctx,
    const nmo_dsl_enum_decl_t *decl)
{
    if (schema_should_skip_redeclare(ctx, decl->name)) {
        NMO_RETURN_OK();
    }

    nmo_status_t st = schema_validate_underlying_type(
        ctx->registry, decl->underlying_type, NMO_TYPE_GUID_INT, "enum", "int32");
    if (st != NMO_OK) return st;

    nmo_enum_value_def_t *values = NULL;
    int64_t default_value = 0;
    st = schema_build_enum_values(decl, &values, &default_value);
    if (st != NMO_OK) return st;

    nmo_enum_type_def_t def = {
        .name = decl->name,
        .description = NULL,
        .guid = schema_generate_guid(ctx, decl->name),
        .values = values,
        .value_count = decl->entry_count,
        .default_value = default_value,
    };

    nmo_guid_t out_guid;
    st = nmo_type_registry_register_enum(ctx->registry, &def, &out_guid);
    free(values);
    return st;
}

static nmo_status_t schema_apply_flags_decl(
    const schema_apply_context_t *ctx,
    const nmo_dsl_flags_decl_t *decl)
{
    if (schema_should_skip_redeclare(ctx, decl->name)) {
        NMO_RETURN_OK();
    }

    nmo_status_t st = schema_validate_underlying_type(
        ctx->registry, decl->underlying_type, NMO_TYPE_GUID_UINT32, "flags", "uint32");
    if (st != NMO_OK) return st;

    nmo_flags_bit_def_t *bits = NULL;
    st = schema_build_flags_bits(decl, &bits);
    if (st != NMO_OK) return st;

    nmo_flags_type_def_t def = {
        .name = decl->name,
        .description = NULL,
        .guid = schema_generate_guid(ctx, decl->name),
        .bits = bits,
        .bit_count = decl->entry_count,
        .default_value = 0,
    };

    nmo_guid_t out_guid;
    st = nmo_type_registry_register_flags(ctx->registry, &def, &out_guid);
    free(bits);
    return st;
}

static nmo_status_t schema_apply_struct_decl(
    const schema_apply_context_t *ctx,
    const nmo_dsl_struct_decl_t *decl)
{
    if (schema_should_skip_redeclare(ctx, decl->name)) {
        NMO_RETURN_OK();
    }

    nmo_struct_field_def_t *fields = NULL;
    nmo_status_t st = schema_build_struct_fields(decl, &fields);
    if (st != NMO_OK) return st;

    nmo_guid_t base_type_guid = NMO_GUID_NULL;
    st = schema_resolve_struct_base(ctx->registry, decl->base_name, &base_type_guid);
    if (st != NMO_OK) {
        schema_free_struct_fields(decl, fields);
        return st;
    }

    nmo_struct_type_def_t def = {
        .name = decl->name,
        .description = NULL,
        .guid = schema_generate_guid(ctx, decl->name),
        .base_type_guid = base_type_guid,
        .fields = fields,
        .field_count = decl->field_count,
        .alignment = decl->alignment,
        .packed = decl->is_packed,
    };

    nmo_guid_t out_guid;
    st = nmo_type_registry_register_struct(ctx->registry, &def, &out_guid);
    schema_free_struct_fields(decl, fields);
    return st;
}

static nmo_status_t schema_apply_alias_decl(
    const schema_apply_context_t *ctx,
    const nmo_dsl_alias_decl_t *decl)
{
    const nmo_type_descriptor_t *target =
        nmo_type_registry_find_by_name(ctx->registry, decl->target_name);
    if (!target) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "alias target '%s' not found", decl->target_name);
    }

    const nmo_type_descriptor_t *existing =
        nmo_type_registry_find_by_name(ctx->registry, decl->name);
    if (existing) {
        if (ctx->options && ctx->options->allow_alias_existing &&
            nmo_guid_equals(existing->guid, target->guid)) {
            NMO_RETURN_OK();
        }
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "alias name '%s' already exists", decl->name);
    }

    nmo_type_descriptor_t alias_desc = *target;
    alias_desc.name = decl->name;
    alias_desc.guid = schema_generate_guid(ctx, decl->name);
    nmo_status_t st = nmo_type_registry_register(ctx->registry, &alias_desc);
    if (st != NMO_OK) return st;

    /* Preserve specialized metadata for enum/flags/struct aliases. */
    if (target->specialized_index != NMO_SPECIALIZED_INDEX_INVALID) {
        const nmo_type_descriptor_t *alias =
            nmo_type_registry_find_by_guid(ctx->registry, alias_desc.guid);
        if (!alias) {
            (void)nmo_type_registry_unregister(ctx->registry, alias_desc.guid);
            NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                             "alias registration succeeded but alias lookup failed");
        }

        const nmo_specialized_metadata_t *target_meta =
            nmo_type_registry_get_metadata(ctx->registry, target->id);
        if (!target_meta) {
            (void)nmo_type_registry_unregister(ctx->registry, alias_desc.guid);
            NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                             "target type metadata missing for alias");
        }

        nmo_specialized_metadata_t alias_meta = *target_meta;
        alias_meta.type_id = alias->id;
        st = nmo_type_registry_register_metadata(ctx->registry, &alias_meta);
        if (st != NMO_OK) {
            (void)nmo_type_registry_unregister(ctx->registry, alias_desc.guid);
            return st;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t schema_apply_decl(
    const schema_apply_context_t *ctx,
    const nmo_dsl_stmt_t *stmt)
{
    switch (stmt->kind) {
        case NMO_DSL_STMT_ENUM_DECL:
            return schema_apply_enum_decl(ctx, &stmt->as.enum_decl);
        case NMO_DSL_STMT_FLAGS_DECL:
            return schema_apply_flags_decl(ctx, &stmt->as.flags_decl);
        case NMO_DSL_STMT_STRUCT_DECL:
            return schema_apply_struct_decl(ctx, &stmt->as.struct_decl);
        case NMO_DSL_STMT_ALIAS_DECL:
            return schema_apply_alias_decl(ctx, &stmt->as.alias_decl);
        default:
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "unexpected statement kind in schema");
    }
}

static nmo_status_t schema_apply_decls(
    nmo_type_registry_t *registry,
    const nmo_dsl_schema_options_t *options,
    const nmo_dsl_stmt_t *decls)
{
    schema_apply_context_t ctx = {
        .registry = registry,
        .options = options,
    };

    for (const nmo_dsl_stmt_t *stmt = decls; stmt; stmt = stmt->next) {
        nmo_status_t st = schema_apply_decl(&ctx, stmt);
        if (st != NMO_OK) return st;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_dsl_apply_schema_ex(
    nmo_type_registry_t *registry,
    const nmo_dsl_program_t *program,
    const nmo_dsl_schema_options_t *options)
{
    if (!registry || !program) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "invalid args");
    }

    if (program->mode != NMO_DSL_MODE_SCHEMA && program->mode != NMO_DSL_MODE_MODULE) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "program not compiled in schema/module mode");
    }

    if (!program->schema_decls) {
        NMO_RETURN_OK();
    }

    return schema_apply_decls(registry, options, program->schema_decls);
}

nmo_status_t nmo_dsl_apply_schema(
    nmo_type_registry_t *registry,
    const nmo_dsl_program_t *program)
{
    return nmo_dsl_apply_schema_ex(registry, program, NULL);
}

nmo_status_t nmo_dsl_run_module(
    nmo_type_registry_t *registry,
    const nmo_dsl_program_t *program,
    const nmo_dsl_eval_context_t *ctx,
    const nmo_dsl_schema_options_t *schema_options,
    nmo_dsl_value_t *out_last_value)
{
    if (!program || !ctx || !registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "invalid args");
    }
    if (program->mode != NMO_DSL_MODE_MODULE) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "program not compiled in module mode");
    }

    nmo_status_t st = nmo_dsl_apply_schema_ex(registry, program, schema_options);
    if (st != NMO_OK) return st;

    nmo_dsl_eval_context_t exec_ctx = *ctx;
    if (!exec_ctx.registry) {
        exec_ctx.registry = registry;
    } else if (exec_ctx.registry != registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "ctx registry must match module registry");
    }

    if (!program->stmts) {
        if (out_last_value) memset(out_last_value, 0, sizeof(*out_last_value));
        NMO_RETURN_OK();
    }

    return nmo_dsl_exec(program, &exec_ctx, out_last_value);
}

/* ============================================================================
 * Value destroy
 * ============================================================================ */

void nmo_dsl_value_destroy(nmo_dsl_value_t *value) {
    if (!value) return;
    if (value->kind == NMO_DSL_VALUE_STRING && value->as.s) {
        free((void *)value->as.s);
        value->as.s = NULL;
    }
    if (value->kind == NMO_DSL_VALUE_SEQ && value->as.seq) {
        nmo_dsl_seq_destroy(value->as.seq);
        value->as.seq = NULL;
    }
    value->kind = NMO_DSL_VALUE_NULL;
}
