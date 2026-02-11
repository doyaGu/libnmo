#include "dsl/nmo_dsl_parse.h"

#include <stdio.h>
#include <string.h>

#define NMO_DSL_MAX_CALL_ARGS 16
#define NMO_DSL_MAX_SCHEMA_ENTRIES 128
#define NMO_DSL_MAX_STRUCT_FIELDS 128

/* ============================================================================
 * Helpers
 * ============================================================================ */

static bool tok_is(nmo_dsl_parser_t *ps, nmo_dsl_tok_kind_t k) {
    return ps->lx.tok.kind == k;
}

static bool tok_accept(nmo_dsl_parser_t *ps, nmo_dsl_tok_kind_t k) {
    if (tok_is(ps, k)) {
        nmo_dsl_lexer_next(&ps->lx);
        return true;
    }
    return false;
}

static bool tok_expect(nmo_dsl_parser_t *ps, nmo_dsl_tok_kind_t k, const char *msg) {
    if (tok_accept(ps, k)) return true;
    (void)snprintf(ps->lx.err, sizeof(ps->lx.err), "%s", msg ? msg : "unexpected token");
    ps->had_error = true;
    return false;
}

static bool tok_is_int_literal(nmo_dsl_parser_t *ps) {
    return tok_is(ps, NMO_DSL_TOK_INT) || tok_is(ps, NMO_DSL_TOK_HEX_INT);
}

static bool parse_fail(nmo_dsl_parser_t *ps, const char *msg) {
    (void)snprintf(ps->lx.err, sizeof(ps->lx.err), "%s", msg ? msg : "parse error");
    ps->had_error = true;
    return false;
}

static const char *arena_strdup_tok(nmo_dsl_parser_t *ps) {
    const nmo_dsl_token_t *t = &ps->lx.tok;
    if (!t->start || t->len == 0) return NULL;
    char *s = (char *)nmo_arena_alloc(ps->arena, t->len + 1, 1);
    if (!s) return NULL;
    memcpy(s, t->start, t->len);
    s[t->len] = '\0';
    return s;
}

static int precedence(nmo_dsl_tok_kind_t op) {
    switch (op) {
        case NMO_DSL_TOK_OROR: return 1;
        case NMO_DSL_TOK_ANDAND: return 2;
        case NMO_DSL_TOK_EQEQ:
        case NMO_DSL_TOK_NEQ: return 3;
        case NMO_DSL_TOK_LT:
        case NMO_DSL_TOK_LE:
        case NMO_DSL_TOK_GT:
        case NMO_DSL_TOK_GE: return 4;
        case NMO_DSL_TOK_PLUS:
        case NMO_DSL_TOK_MINUS: return 5;
        case NMO_DSL_TOK_STAR:
        case NMO_DSL_TOK_SLASH:
        case NMO_DSL_TOK_PERCENT: return 6;
        default: return 0;
    }
}

/* ============================================================================
 * Forward declarations
 * ============================================================================ */

static nmo_dsl_expr_t *parse_expr(nmo_dsl_parser_t *ps, int min_prec);
static nmo_dsl_stmt_t *parse_statement(nmo_dsl_parser_t *ps);
static bool is_schema_decl_start(nmo_dsl_parser_t *ps);

static nmo_dsl_expr_t *expr_alloc_span(
    nmo_dsl_parser_t *ps,
    nmo_dsl_expr_kind_t kind,
    nmo_dsl_span_t span)
{
    nmo_dsl_expr_t *e = nmo_dsl_expr_alloc(ps->arena, kind);
    if (!e) return NULL;
    e->span = span;
    return e;
}

static nmo_dsl_expr_t *expr_alloc_from_token(
    nmo_dsl_parser_t *ps,
    nmo_dsl_expr_kind_t kind)
{
    return expr_alloc_span(ps, kind, nmo_dsl_span_from_token(&ps->lx.tok));
}

static nmo_dsl_expr_t *expr_make_literal_int(
    nmo_dsl_parser_t *ps,
    nmo_dsl_span_t span,
    int64_t value)
{
    nmo_dsl_expr_t *e = expr_alloc_span(ps, NMO_DSL_EXPR_LITERAL, span);
    if (!e) return NULL;
    e->as.lit.kind = NMO_DSL_LIT_INT;
    e->as.lit.as.i = value;
    return e;
}

static nmo_dsl_expr_t *expr_make_binary(
    nmo_dsl_parser_t *ps,
    nmo_dsl_span_t span,
    nmo_dsl_tok_kind_t op,
    nmo_dsl_expr_t *lhs,
    nmo_dsl_expr_t *rhs)
{
    nmo_dsl_expr_t *e = expr_alloc_span(ps, NMO_DSL_EXPR_BINARY, span);
    if (!e) return NULL;
    e->as.binary.op = op;
    e->as.binary.lhs = lhs;
    e->as.binary.rhs = rhs;
    return e;
}

static nmo_dsl_expr_t *expr_make_member(
    nmo_dsl_parser_t *ps,
    nmo_dsl_expr_t *base,
    const char *field)
{
    nmo_dsl_expr_t *e = expr_alloc_span(ps, NMO_DSL_EXPR_MEMBER, base->span);
    if (!e) return NULL;
    e->as.member.base = base;
    e->as.member.field = field;
    return e;
}

static nmo_dsl_expr_t *expr_make_index(
    nmo_dsl_parser_t *ps,
    nmo_dsl_expr_t *base,
    nmo_dsl_expr_t *index)
{
    nmo_dsl_expr_t *e = expr_alloc_span(ps, NMO_DSL_EXPR_INDEX, base->span);
    if (!e) return NULL;
    e->as.index.base = base;
    e->as.index.index = index;
    return e;
}

static nmo_dsl_expr_t *expr_make_slice(
    nmo_dsl_parser_t *ps,
    nmo_dsl_expr_t *base,
    nmo_dsl_expr_t *start,
    nmo_dsl_expr_t *end)
{
    nmo_dsl_expr_t *e = expr_alloc_span(ps, NMO_DSL_EXPR_SLICE, base->span);
    if (!e) return NULL;
    e->as.slice.base = base;
    e->as.slice.start = start;
    e->as.slice.end = end;
    return e;
}

static nmo_dsl_expr_t *expr_make_filter(
    nmo_dsl_parser_t *ps,
    nmo_dsl_expr_t *base,
    nmo_dsl_expr_t *pred)
{
    nmo_dsl_expr_t *e = expr_alloc_span(ps, NMO_DSL_EXPR_FILTER, base->span);
    if (!e) return NULL;
    e->as.filter.base = base;
    e->as.filter.pred = pred;
    return e;
}

static nmo_dsl_expr_t *expr_make_wildcard(nmo_dsl_parser_t *ps, nmo_dsl_expr_t *base) {
    nmo_dsl_expr_t *e = expr_alloc_span(ps, NMO_DSL_EXPR_WILDCARD, base->span);
    if (!e) return NULL;
    e->as.wild_base = base;
    return e;
}

/* ============================================================================
 * Primary
 * ============================================================================ */

static bool parse_call_args(
    nmo_dsl_parser_t *ps,
    nmo_dsl_expr_t ***out_args,
    size_t *out_arg_count)
{
    nmo_dsl_expr_t *arg_buf[NMO_DSL_MAX_CALL_ARGS];
    size_t arg_count = 0;

    if (!out_args || !out_arg_count) return false;
    *out_args = NULL;
    *out_arg_count = 0;

    if (!tok_is(ps, NMO_DSL_TOK_RPAREN)) {
        while (true) {
            nmo_dsl_expr_t *a = parse_expr(ps, 0);
            if (!a) return false;
            if (arg_count >= NMO_DSL_MAX_CALL_ARGS) {
                return parse_fail(ps, "too many args");
            }
            arg_buf[arg_count++] = a;
            if (!tok_accept(ps, NMO_DSL_TOK_COMMA)) break;
        }
    }

    if (!tok_expect(ps, NMO_DSL_TOK_RPAREN, "expected ')'")) return false;

    if (arg_count > 0) {
        nmo_dsl_expr_t **args = (nmo_dsl_expr_t **)nmo_arena_alloc(
            ps->arena, arg_count * sizeof(*args), sizeof(void *));
        if (!args) return false;
        memcpy(args, arg_buf, arg_count * sizeof(*args));
        *out_args = args;
    }
    *out_arg_count = arg_count;
    return true;
}

static nmo_dsl_expr_t *parse_ident_or_call(nmo_dsl_parser_t *ps) {
    const char *name = arena_strdup_tok(ps);
    if (!name) {
        parse_fail(ps, "oom");
        return NULL;
    }

    nmo_dsl_span_t span = nmo_dsl_span_from_token(&ps->lx.tok);
    nmo_dsl_lexer_next(&ps->lx);

    if (!tok_accept(ps, NMO_DSL_TOK_LPAREN)) {
        nmo_dsl_expr_t *ident = expr_alloc_span(ps, NMO_DSL_EXPR_IDENT, span);
        if (!ident) return NULL;
        ident->as.ident = name;
        return ident;
    }

    nmo_dsl_expr_t *call = expr_alloc_span(ps, NMO_DSL_EXPR_CALL, span);
    if (!call) return NULL;
    call->as.call.name = name;

    if (!parse_call_args(ps, &call->as.call.args, &call->as.call.arg_count)) {
        return NULL;
    }

    return call;
}

static nmo_dsl_expr_t *parse_primary(nmo_dsl_parser_t *ps) {
    if (tok_is_int_literal(ps)) {
        nmo_dsl_expr_t *e = expr_alloc_from_token(ps, NMO_DSL_EXPR_LITERAL);
        if (!e) return NULL;
        e->as.lit.kind = NMO_DSL_LIT_INT;
        e->as.lit.as.i = ps->lx.tok.val.i64;
        nmo_dsl_lexer_next(&ps->lx);
        return e;
    }

    if (tok_is(ps, NMO_DSL_TOK_REAL)) {
        nmo_dsl_expr_t *e = expr_alloc_from_token(ps, NMO_DSL_EXPR_LITERAL);
        if (!e) return NULL;
        e->as.lit.kind = NMO_DSL_LIT_REAL;
        e->as.lit.as.r = ps->lx.tok.val.r64;
        nmo_dsl_lexer_next(&ps->lx);
        return e;
    }

    if (tok_is(ps, NMO_DSL_TOK_STRING)) {
        nmo_dsl_expr_t *e = expr_alloc_from_token(ps, NMO_DSL_EXPR_LITERAL);
        if (!e) return NULL;
        e->as.lit.kind = NMO_DSL_LIT_STRING;
        e->as.lit.as.s = ps->lx.tok.val.str; /* arena-owned */
        nmo_dsl_lexer_next(&ps->lx);
        return e;
    }

    if (tok_is(ps, NMO_DSL_TOK_KW_TRUE) || tok_is(ps, NMO_DSL_TOK_KW_FALSE) ||
        tok_is(ps, NMO_DSL_TOK_KW_NULL)) {
        nmo_dsl_expr_t *e = expr_alloc_from_token(ps, NMO_DSL_EXPR_LITERAL);
        if (!e) return NULL;

        if (tok_is(ps, NMO_DSL_TOK_KW_TRUE)) {
            e->as.lit.kind = NMO_DSL_LIT_BOOL;
            e->as.lit.as.b = true;
        } else if (tok_is(ps, NMO_DSL_TOK_KW_FALSE)) {
            e->as.lit.kind = NMO_DSL_LIT_BOOL;
            e->as.lit.as.b = false;
        } else {
            e->as.lit.kind = NMO_DSL_LIT_NULL;
        }
        nmo_dsl_lexer_next(&ps->lx);
        return e;
    }

    if (tok_is(ps, NMO_DSL_TOK_AT)) {
        nmo_dsl_expr_t *e = expr_alloc_from_token(ps, NMO_DSL_EXPR_IDENT);
        if (!e) return NULL;
        e->as.ident = nmo_arena_strdup(ps->arena, "@");
        nmo_dsl_lexer_next(&ps->lx);
        return e;
    }

    if (tok_is(ps, NMO_DSL_TOK_IDENT)) {
        return parse_ident_or_call(ps);
    }

    if (tok_accept(ps, NMO_DSL_TOK_LPAREN)) {
        nmo_dsl_expr_t *e = parse_expr(ps, 0);
        if (!e) return NULL;
        if (!tok_expect(ps, NMO_DSL_TOK_RPAREN, "expected ')'")) return NULL;
        return e;
    }

    parse_fail(ps, "expected expression");
    return NULL;
}

/* ============================================================================
 * Postfix (member, index, wildcard, filter, slice)
 * ============================================================================ */

static nmo_dsl_expr_t *parse_optional_int_literal_expr(nmo_dsl_parser_t *ps) {
    if (!tok_is_int_literal(ps)) return NULL;
    nmo_dsl_span_t span = nmo_dsl_span_from_token(&ps->lx.tok);
    int64_t value = ps->lx.tok.val.i64;
    nmo_dsl_lexer_next(&ps->lx);
    return expr_make_literal_int(ps, span, value);
}

static nmo_dsl_expr_t *parse_index_expr_from_first(
    nmo_dsl_parser_t *ps,
    nmo_dsl_expr_t *lhs)
{
    while (true) {
        nmo_dsl_tok_kind_t op = ps->lx.tok.kind;
        int prec = precedence(op);
        if (prec == 0) break;
        nmo_dsl_lexer_next(&ps->lx);
        nmo_dsl_expr_t *rhs = parse_expr(ps, prec + 1);
        if (!rhs) return NULL;
        lhs = expr_make_binary(ps, lhs->span, op, lhs, rhs);
        if (!lhs) return NULL;
    }
    return lhs;
}

static nmo_dsl_expr_t *parse_bracket_suffix(
    nmo_dsl_parser_t *ps,
    nmo_dsl_expr_t *base)
{
    if (tok_accept(ps, NMO_DSL_TOK_STAR)) {
        if (!tok_expect(ps, NMO_DSL_TOK_RBRACK, "expected ']'")) return NULL;
        return expr_make_wildcard(ps, base);
    }

    if (tok_accept(ps, NMO_DSL_TOK_QUESTION)) {
        nmo_dsl_expr_t *pred = parse_expr(ps, 0);
        if (!pred) return NULL;
        if (!tok_expect(ps, NMO_DSL_TOK_RBRACK, "expected ']'")) return NULL;
        return expr_make_filter(ps, base, pred);
    }

    if (tok_accept(ps, NMO_DSL_TOK_COLON)) {
        nmo_dsl_expr_t *end_expr = parse_optional_int_literal_expr(ps);
        if (!tok_expect(ps, NMO_DSL_TOK_RBRACK, "expected ']'")) return NULL;
        return expr_make_slice(ps, base, NULL, end_expr);
    }

    if (tok_is_int_literal(ps)) {
        nmo_dsl_expr_t *start_or_index = parse_optional_int_literal_expr(ps);
        if (!start_or_index) return NULL;

        if (tok_accept(ps, NMO_DSL_TOK_COLON)) {
            nmo_dsl_expr_t *end_expr = parse_optional_int_literal_expr(ps);
            if (!tok_expect(ps, NMO_DSL_TOK_RBRACK, "expected ']'")) return NULL;
            return expr_make_slice(ps, base, start_or_index, end_expr);
        }

        nmo_dsl_expr_t *idx = parse_index_expr_from_first(ps, start_or_index);
        if (!idx) return NULL;
        if (!tok_expect(ps, NMO_DSL_TOK_RBRACK, "expected ']'")) return NULL;
        return expr_make_index(ps, base, idx);
    }

    nmo_dsl_expr_t *idx = parse_expr(ps, 0);
    if (!idx) return NULL;
    if (!tok_expect(ps, NMO_DSL_TOK_RBRACK, "expected ']'")) return NULL;
    return expr_make_index(ps, base, idx);
}

static nmo_dsl_expr_t *parse_postfix(nmo_dsl_parser_t *ps, nmo_dsl_expr_t *base) {
    while (base) {
        if (tok_accept(ps, NMO_DSL_TOK_DOT)) {
            if (!tok_is(ps, NMO_DSL_TOK_IDENT)) {
                parse_fail(ps, "expected field name");
                return NULL;
            }
            const char *field = arena_strdup_tok(ps);
            if (!field) return NULL;
            nmo_dsl_lexer_next(&ps->lx);
            base = expr_make_member(ps, base, field);
            if (!base) return NULL;
            continue;
        }

        if (tok_accept(ps, NMO_DSL_TOK_LBRACK)) {
            base = parse_bracket_suffix(ps, base);
            if (!base) return NULL;
            continue;
        }

        break;
    }

    return base;
}

/* ============================================================================
 * Unary
 * ============================================================================ */

static nmo_dsl_expr_t *parse_unary(nmo_dsl_parser_t *ps) {
    if (tok_is(ps, NMO_DSL_TOK_MINUS) || tok_is(ps, NMO_DSL_TOK_BANG) || tok_is(ps, NMO_DSL_TOK_PLUS)) {
        nmo_dsl_tok_kind_t op = ps->lx.tok.kind;
        nmo_dsl_span_t span = nmo_dsl_span_from_token(&ps->lx.tok);
        nmo_dsl_lexer_next(&ps->lx);
        nmo_dsl_expr_t *rhs = parse_unary(ps);
        if (!rhs) return NULL;
        nmo_dsl_expr_t *e = expr_alloc_span(ps, NMO_DSL_EXPR_UNARY, span);
        if (!e) return NULL;
        e->as.unary.op = op;
        e->as.unary.rhs = rhs;
        return e;
    }

    nmo_dsl_expr_t *p = parse_primary(ps);
    if (!p) return NULL;
    return parse_postfix(ps, p);
}

/* ============================================================================
 * Expression (Pratt)
 * ============================================================================ */

static nmo_dsl_expr_t *parse_expr(nmo_dsl_parser_t *ps, int min_prec) {
    nmo_dsl_expr_t *lhs = parse_unary(ps);
    if (!lhs) return NULL;

    while (true) {
        nmo_dsl_tok_kind_t op = ps->lx.tok.kind;
        int prec = precedence(op);
        if (prec == 0 || prec < min_prec) break;

        nmo_dsl_lexer_next(&ps->lx);
        nmo_dsl_expr_t *rhs = parse_expr(ps, prec + 1);
        if (!rhs) return NULL;

        nmo_dsl_expr_t *e = expr_make_binary(ps, lhs->span, op, lhs, rhs);
        if (!e) return NULL;
        lhs = e;
    }

    return lhs;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void nmo_dsl_parser_init(nmo_dsl_parser_t *ps, const char *source, nmo_arena_t *arena) {
    memset(ps, 0, sizeof(*ps));
    ps->arena = arena;
    ps->had_error = false;
    nmo_dsl_lexer_init(&ps->lx, source, arena);
}

nmo_dsl_expr_t *nmo_dsl_parse_expression(nmo_dsl_parser_t *ps) {
    return parse_expr(ps, 0);
}

/* ============================================================================
 * Script Parsing
 * ============================================================================ */

static bool is_lvalue(const nmo_dsl_expr_t *e) {
    if (!e) return false;
    switch (e->kind) {
        case NMO_DSL_EXPR_IDENT:
            return true;
        case NMO_DSL_EXPR_MEMBER:
            return is_lvalue(e->as.member.base);
        case NMO_DSL_EXPR_INDEX:
            /* Keep parser/runtime alignment: assignment by index only supports
             * array bases rooted at identifiers/member chains. */
            return e->as.index.base &&
                   (e->as.index.base->kind == NMO_DSL_EXPR_IDENT ||
                    e->as.index.base->kind == NMO_DSL_EXPR_MEMBER);
        default:
            return false;
    }
}

static nmo_dsl_stmt_t *parse_statement(nmo_dsl_parser_t *ps) {
    if (tok_is(ps, NMO_DSL_TOK_EOF)) {
        parse_fail(ps, "expected statement");
        return NULL;
    }

    nmo_dsl_expr_t *expr = parse_expr(ps, 0);
    if (!expr) return NULL;

    /* assignment: expr = rhs */
    if (tok_is(ps, NMO_DSL_TOK_EQ)) {
        if (!is_lvalue(expr)) {
            parse_fail(ps, "left side of assignment must be an lvalue");
            return NULL;
        }
        nmo_dsl_lexer_next(&ps->lx);  /* consume '=' */
        nmo_dsl_expr_t *rhs = parse_expr(ps, 0);
        if (!rhs) return NULL;

        nmo_dsl_stmt_t *s = nmo_dsl_stmt_alloc(ps->arena, NMO_DSL_STMT_ASSIGN);
        if (!s) return NULL;
        s->span = expr->span;
        s->as.assign.target = expr;
        s->as.assign.value = rhs;
        return s;
    }

    /* expression statement */
    nmo_dsl_stmt_t *s = nmo_dsl_stmt_alloc(ps->arena, NMO_DSL_STMT_EXPR);
    if (!s) return NULL;
    s->span = expr->span;
    s->as.expr = expr;
    return s;
}

static nmo_dsl_stmt_t *parse_statement_list_after_first(
    nmo_dsl_parser_t *ps,
    nmo_dsl_stmt_t *first,
    const char *schema_after_script_error)
{
    nmo_dsl_stmt_t *head = first;
    nmo_dsl_stmt_t *tail = first;

    while (tok_accept(ps, NMO_DSL_TOK_SEMICOLON)) {
        if (tok_is(ps, NMO_DSL_TOK_EOF)) break;
        if (schema_after_script_error &&
            (tok_is(ps, NMO_DSL_TOK_KW_SCHEMA) || is_schema_decl_start(ps))) {
            parse_fail(ps, schema_after_script_error);
            return NULL;
        }

        nmo_dsl_stmt_t *next = parse_statement(ps);
        if (!next) return NULL;
        tail->next = next;
        tail = next;
    }

    return head;
}

nmo_dsl_stmt_t *nmo_dsl_parse_script(nmo_dsl_parser_t *ps) {
    /* Handle empty program */
    if (tok_is(ps, NMO_DSL_TOK_EOF)) return NULL;

    nmo_dsl_stmt_t *first = parse_statement(ps);
    if (!first) return NULL;
    return parse_statement_list_after_first(ps, first, NULL);
}

/* ============================================================================
 * Schema Parsing
 * ============================================================================ */

static const char *expect_ident(nmo_dsl_parser_t *ps) {
    if (!tok_is(ps, NMO_DSL_TOK_IDENT)) {
        parse_fail(ps, "expected identifier");
        return NULL;
    }
    const char *name = arena_strdup_tok(ps);
    nmo_dsl_lexer_next(&ps->lx);
    return name;
}

typedef struct {
    const char *name;
    bool has_value;
    int64_t value;
} nmo_dsl_named_num_entry_t;

static bool parse_required_underlying_type(nmo_dsl_parser_t *ps, const char **out_underlying) {
    if (!ps || !out_underlying) return false;
    *out_underlying = NULL;
    if (!tok_expect(ps, NMO_DSL_TOK_COLON, "expected ':' before underlying type")) return false;
    const char *underlying = expect_ident(ps);
    if (!underlying) return false;
    *out_underlying = underlying;
    return true;
}

static bool parse_optional_assigned_int(nmo_dsl_parser_t *ps, bool *out_has_value, int64_t *out_value) {
    if (!ps || !out_has_value || !out_value) return false;
    *out_has_value = false;
    *out_value = 0;
    if (!tok_accept(ps, NMO_DSL_TOK_EQ)) return true;
    if (!tok_is_int_literal(ps)) return parse_fail(ps, "expected integer value");
    *out_has_value = true;
    *out_value = ps->lx.tok.val.i64;
    nmo_dsl_lexer_next(&ps->lx);
    return true;
}

static bool parse_entry_separator(nmo_dsl_parser_t *ps) {
    if (!ps) return false;
    if (tok_accept(ps, NMO_DSL_TOK_COMMA)) return true;
    if (!tok_is(ps, NMO_DSL_TOK_RBRACE)) {
        return parse_fail(ps, "expected ',' or '}'");
    }
    return true;
}

static bool parse_named_numeric_entries(
    nmo_dsl_parser_t *ps,
    nmo_dsl_named_num_entry_t *entries,
    size_t capacity,
    size_t *out_count,
    const char *too_many_msg)
{
    if (!ps || !entries || !out_count || capacity == 0) return false;
    *out_count = 0;
    while (!tok_is(ps, NMO_DSL_TOK_RBRACE) && !tok_is(ps, NMO_DSL_TOK_EOF)) {
        if (*out_count >= capacity) {
            return parse_fail(ps, too_many_msg);
        }
        const char *ename = expect_ident(ps);
        if (!ename) return false;

        nmo_dsl_named_num_entry_t e = {0};
        e.name = ename;
        if (!parse_optional_assigned_int(ps, &e.has_value, &e.value)) return false;
        entries[(*out_count)++] = e;

        if (!parse_entry_separator(ps)) return false;
        if (tok_is(ps, NMO_DSL_TOK_RBRACE)) break;
    }
    return tok_expect(ps, NMO_DSL_TOK_RBRACE, "expected '}'");
}

typedef enum {
    NMO_DSL_NUMERIC_DECL_ENUM,
    NMO_DSL_NUMERIC_DECL_FLAGS,
} nmo_dsl_numeric_decl_kind_t;

static bool parse_numeric_decl_header(
    nmo_dsl_parser_t *ps,
    nmo_dsl_span_t *out_span,
    const char **out_name,
    const char **out_underlying)
{
    if (!ps || !out_span || !out_name || !out_underlying) return false;
    *out_span = nmo_dsl_span_from_token(&ps->lx.tok);
    *out_name = NULL;
    *out_underlying = NULL;

    nmo_dsl_lexer_next(&ps->lx); /* consume enum/flags keyword */

    const char *name = expect_ident(ps);
    if (!name) return false;
    const char *underlying = NULL;
    if (!parse_required_underlying_type(ps, &underlying)) return false;
    if (!tok_expect(ps, NMO_DSL_TOK_LBRACE, "expected '{'")) return false;

    *out_name = name;
    *out_underlying = underlying;
    return true;
}

static nmo_dsl_stmt_t *parse_numeric_decl(
    nmo_dsl_parser_t *ps,
    nmo_dsl_numeric_decl_kind_t kind)
{
    const char *too_many_msg = (kind == NMO_DSL_NUMERIC_DECL_ENUM)
        ? "too many enum entries"
        : "too many flags entries";

    nmo_dsl_span_t span = {0};
    const char *name = NULL;
    const char *underlying = NULL;
    if (!parse_numeric_decl_header(ps, &span, &name, &underlying)) return NULL;

    nmo_dsl_named_num_entry_t parsed_entries[NMO_DSL_MAX_SCHEMA_ENTRIES];
    size_t count = 0;
    if (!parse_named_numeric_entries(
            ps, parsed_entries, NMO_DSL_MAX_SCHEMA_ENTRIES, &count, too_many_msg)) {
        return NULL;
    }

    nmo_dsl_stmt_kind_t stmt_kind = (kind == NMO_DSL_NUMERIC_DECL_ENUM)
        ? NMO_DSL_STMT_ENUM_DECL
        : NMO_DSL_STMT_FLAGS_DECL;
    nmo_dsl_stmt_t *s = nmo_dsl_stmt_alloc(ps->arena, stmt_kind);
    if (!s) return NULL;
    s->span = span;

    if (kind == NMO_DSL_NUMERIC_DECL_ENUM) {
        s->as.enum_decl.name = name;
        s->as.enum_decl.underlying_type = underlying;
        s->as.enum_decl.entry_count = count;
        if (count > 0) {
            nmo_dsl_enum_entry_t *arr = (nmo_dsl_enum_entry_t *)nmo_arena_alloc(
                ps->arena, count * sizeof(*arr), sizeof(void *));
            if (!arr) return NULL;
            for (size_t i = 0; i < count; i++) {
                arr[i].name = parsed_entries[i].name;
                arr[i].has_value = parsed_entries[i].has_value;
                arr[i].value = parsed_entries[i].value;
            }
            s->as.enum_decl.entries = arr;
        }
        return s;
    }

    s->as.flags_decl.name = name;
    s->as.flags_decl.underlying_type = underlying;
    s->as.flags_decl.entry_count = count;
    if (count > 0) {
        nmo_dsl_flags_entry_t *arr = (nmo_dsl_flags_entry_t *)nmo_arena_alloc(
            ps->arena, count * sizeof(*arr), sizeof(void *));
        if (!arr) return NULL;
        for (size_t i = 0; i < count; i++) {
            arr[i].name = parsed_entries[i].name;
            arr[i].has_value = parsed_entries[i].has_value;
            arr[i].value = (uint64_t)parsed_entries[i].value;
        }
        s->as.flags_decl.entries = arr;
    }
    return s;
}

static nmo_dsl_stmt_t *parse_enum_decl(nmo_dsl_parser_t *ps) {
    return parse_numeric_decl(ps, NMO_DSL_NUMERIC_DECL_ENUM);
}

static nmo_dsl_stmt_t *parse_flags_decl(nmo_dsl_parser_t *ps) {
    return parse_numeric_decl(ps, NMO_DSL_NUMERIC_DECL_FLAGS);
}

static bool parse_struct_modifiers(
    nmo_dsl_parser_t *ps,
    bool *out_is_packed,
    uint32_t *out_alignment)
{
    if (!ps || !out_is_packed || !out_alignment) return false;
    *out_is_packed = false;
    *out_alignment = 0;

    if (tok_accept(ps, NMO_DSL_TOK_KW_PACKED)) {
        *out_is_packed = true;
        return true;
    }
    if (!tok_accept(ps, NMO_DSL_TOK_KW_ALIGNED)) {
        return true;
    }

    if (!tok_expect(ps, NMO_DSL_TOK_LPAREN, "expected '('")) return false;
    if (!tok_is(ps, NMO_DSL_TOK_INT)) {
        return parse_fail(ps, "expected alignment value");
    }
    *out_alignment = (uint32_t)ps->lx.tok.val.i64;
    nmo_dsl_lexer_next(&ps->lx);
    if (!tok_expect(ps, NMO_DSL_TOK_RPAREN, "expected ')'")) return false;
    return true;
}

static bool parse_struct_field(nmo_dsl_parser_t *ps, nmo_dsl_struct_field_decl_t *out_field) {
    if (!ps || !out_field) return false;

    const char *type_name = expect_ident(ps);
    if (!type_name) return false;
    const char *field_name = expect_ident(ps);
    if (!field_name) return false;

    nmo_dsl_struct_field_decl_t field = {0};
    field.type_name = type_name;
    field.field_name = field_name;
    if (tok_accept(ps, NMO_DSL_TOK_LBRACK)) {
        if (!tok_expect(ps, NMO_DSL_TOK_RBRACK, "expected ']'")) return false;
        field.is_repeated = true;
    }

    *out_field = field;
    return true;
}

static bool parse_struct_fields(
    nmo_dsl_parser_t *ps,
    nmo_dsl_struct_field_decl_t *fields,
    size_t capacity,
    size_t *out_count)
{
    if (!ps || !fields || !out_count || capacity == 0) return false;
    *out_count = 0;

    while (!tok_is(ps, NMO_DSL_TOK_RBRACE) && !tok_is(ps, NMO_DSL_TOK_EOF)) {
        if (*out_count >= capacity) {
            return parse_fail(ps, "too many struct fields");
        }

        nmo_dsl_struct_field_decl_t field = {0};
        if (!parse_struct_field(ps, &field)) return false;
        fields[(*out_count)++] = field;

        if (tok_accept(ps, NMO_DSL_TOK_SEMICOLON)) {
            if (tok_is(ps, NMO_DSL_TOK_RBRACE)) break; /* trailing ';' */
            continue;
        }
        if (!tok_is(ps, NMO_DSL_TOK_RBRACE)) {
            return parse_fail(ps, "expected ';' or '}'");
        }
    }
    return true;
}

static nmo_dsl_stmt_t *parse_struct_decl(nmo_dsl_parser_t *ps) {
    nmo_dsl_span_t span = nmo_dsl_span_from_token(&ps->lx.tok);
    bool is_packed = false;
    uint32_t alignment = 0;

    if (!parse_struct_modifiers(ps, &is_packed, &alignment)) return NULL;

    if (!tok_expect(ps, NMO_DSL_TOK_KW_STRUCT, "expected 'struct'")) return NULL;

    const char *name = expect_ident(ps);
    if (!name) return NULL;

    /* optional base type */
    const char *base_name = NULL;
    if (tok_accept(ps, NMO_DSL_TOK_COLON)) {
        base_name = expect_ident(ps);
        if (!base_name) return NULL;
    }

    if (!tok_expect(ps, NMO_DSL_TOK_LBRACE, "expected '{'")) return NULL;

    nmo_dsl_struct_field_decl_t fields[NMO_DSL_MAX_STRUCT_FIELDS];
    size_t count = 0;
    if (!parse_struct_fields(ps, fields, NMO_DSL_MAX_STRUCT_FIELDS, &count)) return NULL;

    if (!tok_expect(ps, NMO_DSL_TOK_RBRACE, "expected '}'")) return NULL;

    nmo_dsl_stmt_t *s = nmo_dsl_stmt_alloc(ps->arena, NMO_DSL_STMT_STRUCT_DECL);
    if (!s) return NULL;
    s->span = span;
    s->as.struct_decl.name = name;
    s->as.struct_decl.base_name = base_name;
    s->as.struct_decl.is_packed = is_packed;
    s->as.struct_decl.alignment = alignment;
    s->as.struct_decl.field_count = count;
    if (count > 0) {
        nmo_dsl_struct_field_decl_t *arr = (nmo_dsl_struct_field_decl_t *)nmo_arena_alloc(
            ps->arena, count * sizeof(*arr), sizeof(void *));
        if (!arr) return NULL;
        memcpy(arr, fields, count * sizeof(*arr));
        s->as.struct_decl.fields = arr;
    }
    return s;
}

static nmo_dsl_stmt_t *parse_alias_decl(nmo_dsl_parser_t *ps) {
    nmo_dsl_span_t span = nmo_dsl_span_from_token(&ps->lx.tok);
    nmo_dsl_lexer_next(&ps->lx);  /* consume 'alias' */

    const char *name = expect_ident(ps);
    if (!name) return NULL;

    if (!tok_expect(ps, NMO_DSL_TOK_EQ, "expected '='")) return NULL;

    const char *target = expect_ident(ps);
    if (!target) return NULL;

    nmo_dsl_stmt_t *s = nmo_dsl_stmt_alloc(ps->arena, NMO_DSL_STMT_ALIAS_DECL);
    if (!s) return NULL;
    s->span = span;
    s->as.alias_decl.name = name;
    s->as.alias_decl.target_name = target;
    return s;
}

static bool is_schema_decl_start(nmo_dsl_parser_t *ps) {
    return tok_is(ps, NMO_DSL_TOK_KW_ENUM) ||
           tok_is(ps, NMO_DSL_TOK_KW_FLAGS) ||
           tok_is(ps, NMO_DSL_TOK_KW_STRUCT) ||
           tok_is(ps, NMO_DSL_TOK_KW_PACKED) ||
           tok_is(ps, NMO_DSL_TOK_KW_ALIGNED) ||
           tok_is(ps, NMO_DSL_TOK_KW_ALIAS);
}

static void stmt_list_append(
    nmo_dsl_stmt_t **head,
    nmo_dsl_stmt_t **tail,
    nmo_dsl_stmt_t *stmt)
{
    if (!head || !tail || !stmt) return;
    if (!*head) {
        *head = stmt;
    } else {
        (*tail)->next = stmt;
    }
    *tail = stmt;
}

static nmo_dsl_stmt_t *parse_schema_decl(nmo_dsl_parser_t *ps) {
    if (tok_is(ps, NMO_DSL_TOK_KW_ENUM)) {
        return parse_enum_decl(ps);
    }
    if (tok_is(ps, NMO_DSL_TOK_KW_FLAGS)) {
        return parse_flags_decl(ps);
    }
    if (tok_is(ps, NMO_DSL_TOK_KW_STRUCT) ||
        tok_is(ps, NMO_DSL_TOK_KW_PACKED) ||
        tok_is(ps, NMO_DSL_TOK_KW_ALIGNED)) {
        return parse_struct_decl(ps);
    }
    if (tok_is(ps, NMO_DSL_TOK_KW_ALIAS)) {
        return parse_alias_decl(ps);
    }
    return NULL;
}

static bool parse_schema_block(nmo_dsl_parser_t *ps, nmo_dsl_stmt_t **out_head) {
    if (!ps || !out_head) return false;
    *out_head = NULL;

    if (!tok_expect(ps, NMO_DSL_TOK_KW_SCHEMA, "expected 'schema'")) return false;
    if (!tok_expect(ps, NMO_DSL_TOK_LBRACE, "expected '{' after schema")) return false;

    nmo_dsl_stmt_t *head = NULL;
    nmo_dsl_stmt_t *tail = NULL;
    while (!tok_is(ps, NMO_DSL_TOK_RBRACE) && !tok_is(ps, NMO_DSL_TOK_EOF)) {
        nmo_dsl_stmt_t *decl = parse_schema_decl(ps);
        if (!decl) {
            parse_fail(ps, "expected enum, flags, struct, or alias");
            return false;
        }
        stmt_list_append(&head, &tail, decl);
    }

    if (!tok_expect(ps, NMO_DSL_TOK_RBRACE, "expected '}'")) return false;
    *out_head = head;
    return true;
}

nmo_dsl_stmt_t *nmo_dsl_parse_schema(nmo_dsl_parser_t *ps) {
    nmo_dsl_stmt_t *head = NULL;
    if (!parse_schema_block(ps, &head)) return NULL;
    return head;
}

bool nmo_dsl_parse_module(nmo_dsl_parser_t *ps, nmo_dsl_module_ast_t *out_module) {
    if (!ps || !out_module) return false;
    out_module->schema_decls = NULL;
    out_module->script_stmts = NULL;

    /* Current implementation requires a leading schema block. */
    nmo_dsl_stmt_t *schema_head = NULL;
    if (!tok_is(ps, NMO_DSL_TOK_KW_SCHEMA)) {
        parse_fail(ps, "module must start with schema { ... }");
        return false;
    }
    if (!parse_schema_block(ps, &schema_head)) return false;

    /* Parse trailing script statements (if any) */
    nmo_dsl_stmt_t *script_head = NULL;
    if (!tok_is(ps, NMO_DSL_TOK_EOF)) {
        nmo_dsl_stmt_t *first = parse_statement(ps);
        if (!first) return false;
        script_head = parse_statement_list_after_first(
            ps, first, "schema declarations must precede script statements");
        if (!script_head) return false;
    }

    out_module->schema_decls = schema_head;
    out_module->script_stmts = script_head;
    return true;
}
