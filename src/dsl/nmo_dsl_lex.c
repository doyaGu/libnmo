#include "dsl/nmo_dsl_lex.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_ident_start(char c) {
    return c == '_' || isalpha((unsigned char)c);
}

static bool is_ident_char(char c) {
    return c == '_' || isalnum((unsigned char)c);
}

static void skip_whitespace_and_comments(nmo_dsl_lexer_t *lx) {
    while (*lx->cur) {
        /* whitespace */
        if (isspace((unsigned char)*lx->cur)) {
            if (*lx->cur == '\n') {
                lx->line++;
                lx->col = 1;
            } else {
                lx->col++;
            }
            lx->cur++;
            continue;
        }
        /* line comment */
        if (lx->cur[0] == '/' && lx->cur[1] == '/') {
            lx->cur += 2;
            lx->col += 2;
            while (*lx->cur && *lx->cur != '\n') {
                lx->cur++;
                lx->col++;
            }
            continue;
        }
        /* block comment */
        if (lx->cur[0] == '/' && lx->cur[1] == '*') {
            lx->cur += 2;
            lx->col += 2;
            while (*lx->cur) {
                if (lx->cur[0] == '*' && lx->cur[1] == '/') {
                    lx->cur += 2;
                    lx->col += 2;
                    break;
                }
                if (*lx->cur == '\n') {
                    lx->line++;
                    lx->col = 1;
                } else {
                    lx->col++;
                }
                lx->cur++;
            }
            continue;
        }
        break;
    }
}

static nmo_dsl_tok_kind_t check_keyword(const char *start, size_t len) {
    if (len == 4 && memcmp(start, "true", 4) == 0)    return NMO_DSL_TOK_KW_TRUE;
    if (len == 5 && memcmp(start, "false", 5) == 0)   return NMO_DSL_TOK_KW_FALSE;
    if (len == 4 && memcmp(start, "null", 4) == 0)     return NMO_DSL_TOK_KW_NULL;
    if (len == 6 && memcmp(start, "schema", 6) == 0)  return NMO_DSL_TOK_KW_SCHEMA;
    if (len == 4 && memcmp(start, "enum", 4) == 0)     return NMO_DSL_TOK_KW_ENUM;
    if (len == 5 && memcmp(start, "flags", 5) == 0)   return NMO_DSL_TOK_KW_FLAGS;
    if (len == 6 && memcmp(start, "struct", 6) == 0)  return NMO_DSL_TOK_KW_STRUCT;
    if (len == 5 && memcmp(start, "alias", 5) == 0)   return NMO_DSL_TOK_KW_ALIAS;
    if (len == 6 && memcmp(start, "packed", 6) == 0)  return NMO_DSL_TOK_KW_PACKED;
    if (len == 7 && memcmp(start, "aligned", 7) == 0) return NMO_DSL_TOK_KW_ALIGNED;
    return NMO_DSL_TOK_IDENT;
}

static bool read_string(nmo_dsl_lexer_t *lx) {
    const char *s = lx->cur;
    if (*s != '"') return false;
    s++;

    /* first pass: compute length */
    const char *p = s;
    size_t len = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p) {
                (void)snprintf(lx->err, sizeof(lx->err), "unterminated string");
                return false;
            }
            char e = *p;
            if (e != '\\' && e != '"' && e != 'n' && e != 'r' && e != 't') {
                (void)snprintf(lx->err, sizeof(lx->err), "bad string escape");
                return false;
            }
        }
        p++;
        len++;
    }
    if (*p != '"') {
        (void)snprintf(lx->err, sizeof(lx->err), "unterminated string");
        return false;
    }

    /* allocate from arena */
    char *buf = (char *)nmo_arena_alloc(lx->arena, len + 1, 1);
    if (!buf) {
        (void)snprintf(lx->err, sizeof(lx->err), "oom");
        return false;
    }

    /* second pass: decode */
    p = s;
    size_t i = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            char e = *p++;
            if (e == '\\') c = '\\';
            else if (e == '"') c = '"';
            else if (e == 'n') c = '\n';
            else if (e == 'r') c = '\r';
            else if (e == 't') c = '\t';
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    p++; /* skip closing '"' */

    uint32_t tok_col = lx->col;
    uint32_t tok_offset = (uint32_t)(lx->cur - lx->source);
    size_t tok_len = (size_t)(p - lx->cur);

    lx->tok.kind = NMO_DSL_TOK_STRING;
    lx->tok.start = lx->cur;
    lx->tok.len = tok_len;
    lx->tok.val.str = buf;
    lx->tok.line = lx->line;
    lx->tok.col = tok_col;
    lx->tok.offset = tok_offset;

    lx->col += (uint32_t)tok_len;
    lx->cur = p;
    return true;
}

void nmo_dsl_lexer_init(nmo_dsl_lexer_t *lx, const char *source, nmo_arena_t *arena) {
    memset(lx, 0, sizeof(*lx));
    lx->source = source;
    lx->cur = source;
    lx->arena = arena;
    lx->line = 1;
    lx->col = 1;
    nmo_dsl_lexer_next(lx);
}

void nmo_dsl_lexer_next(nmo_dsl_lexer_t *lx) {
    if (!lx) return;
    memset(&lx->tok, 0, sizeof(lx->tok));

    skip_whitespace_and_comments(lx);

    const char *s = lx->cur;
    if (!s || *s == '\0') {
        lx->tok.kind = NMO_DSL_TOK_EOF;
        lx->tok.start = s;
        lx->tok.len = 0;
        lx->tok.line = lx->line;
        lx->tok.col = lx->col;
        lx->tok.offset = (uint32_t)(s ? (s - lx->source) : 0);
        return;
    }

    uint32_t tok_line = lx->line;
    uint32_t tok_col = lx->col;
    uint32_t tok_offset = (uint32_t)(s - lx->source);

    /* multi-char operators */
    if (s[0] == '&' && s[1] == '&') {
        lx->tok.kind = NMO_DSL_TOK_ANDAND;
        lx->tok.start = s; lx->tok.len = 2;
        lx->tok.line = tok_line; lx->tok.col = tok_col; lx->tok.offset = tok_offset;
        lx->cur += 2; lx->col += 2;
        return;
    }
    if (s[0] == '|' && s[1] == '|') {
        lx->tok.kind = NMO_DSL_TOK_OROR;
        lx->tok.start = s; lx->tok.len = 2;
        lx->tok.line = tok_line; lx->tok.col = tok_col; lx->tok.offset = tok_offset;
        lx->cur += 2; lx->col += 2;
        return;
    }
    if (s[0] == '=' && s[1] == '=') {
        lx->tok.kind = NMO_DSL_TOK_EQEQ;
        lx->tok.start = s; lx->tok.len = 2;
        lx->tok.line = tok_line; lx->tok.col = tok_col; lx->tok.offset = tok_offset;
        lx->cur += 2; lx->col += 2;
        return;
    }
    if (s[0] == '!' && s[1] == '=') {
        lx->tok.kind = NMO_DSL_TOK_NEQ;
        lx->tok.start = s; lx->tok.len = 2;
        lx->tok.line = tok_line; lx->tok.col = tok_col; lx->tok.offset = tok_offset;
        lx->cur += 2; lx->col += 2;
        return;
    }
    if (s[0] == '<' && s[1] == '=') {
        lx->tok.kind = NMO_DSL_TOK_LE;
        lx->tok.start = s; lx->tok.len = 2;
        lx->tok.line = tok_line; lx->tok.col = tok_col; lx->tok.offset = tok_offset;
        lx->cur += 2; lx->col += 2;
        return;
    }
    if (s[0] == '>' && s[1] == '=') {
        lx->tok.kind = NMO_DSL_TOK_GE;
        lx->tok.start = s; lx->tok.len = 2;
        lx->tok.line = tok_line; lx->tok.col = tok_col; lx->tok.offset = tok_offset;
        lx->cur += 2; lx->col += 2;
        return;
    }

    /* string */
    if (*s == '"') {
        if (!read_string(lx)) {
            lx->tok.kind = NMO_DSL_TOK_ERROR;
            lx->tok.start = s;
            lx->tok.len = 0;
            lx->tok.line = tok_line;
            lx->tok.col = tok_col;
            lx->tok.offset = tok_offset;
        }
        return;
    }

    /* number: hex or decimal */
    if (isdigit((unsigned char)*s)) {
        /* hex */
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            const char *p = s + 2;
            while (isxdigit((unsigned char)*p)) p++;
            if (p == s + 2) {
                (void)snprintf(lx->err, sizeof(lx->err), "expected hex digits");
                lx->tok.kind = NMO_DSL_TOK_ERROR;
                lx->tok.start = s; lx->tok.len = 0;
                lx->tok.line = tok_line; lx->tok.col = tok_col; lx->tok.offset = tok_offset;
                return;
            }
            char tmp[64];
            size_t n = (size_t)(p - s);
            if (n >= sizeof(tmp)) {
                (void)snprintf(lx->err, sizeof(lx->err), "number too long");
                lx->tok.kind = NMO_DSL_TOK_ERROR;
                lx->tok.start = s; lx->tok.len = 0;
                lx->tok.line = tok_line; lx->tok.col = tok_col; lx->tok.offset = tok_offset;
                return;
            }
            memcpy(tmp, s, n);
            tmp[n] = '\0';
            lx->tok.kind = NMO_DSL_TOK_HEX_INT;
            lx->tok.start = s;
            lx->tok.len = n;
            lx->tok.val.i64 = strtoll(tmp, NULL, 16);
            lx->tok.line = tok_line; lx->tok.col = tok_col; lx->tok.offset = tok_offset;
            lx->cur = p;
            lx->col += (uint32_t)n;
            return;
        }

        /* decimal */
        const char *p = s;
        bool is_real = false;
        while (isdigit((unsigned char)*p)) p++;
        if (*p == '.' && isdigit((unsigned char)p[1])) {
            is_real = true;
            p++;
            while (isdigit((unsigned char)*p)) p++;
        } else if (*p == '.' && !is_ident_start(p[1])) {
            /* e.g. "3." could be real but we match existing behavior: only if digits follow the dot */
            /* In existing code, "3." is parsed as INT 3 then DOT. Keep that behavior. */
        }

        char tmp[64];
        size_t n = (size_t)(p - s);
        if (n >= sizeof(tmp)) {
            (void)snprintf(lx->err, sizeof(lx->err), "number too long");
            lx->tok.kind = NMO_DSL_TOK_ERROR;
            lx->tok.start = s; lx->tok.len = 0;
            lx->tok.line = tok_line; lx->tok.col = tok_col; lx->tok.offset = tok_offset;
            return;
        }
        memcpy(tmp, s, n);
        tmp[n] = '\0';

        lx->tok.start = s;
        lx->tok.len = n;
        lx->tok.line = tok_line;
        lx->tok.col = tok_col;
        lx->tok.offset = tok_offset;
        if (is_real) {
            lx->tok.kind = NMO_DSL_TOK_REAL;
            lx->tok.val.r64 = strtod(tmp, NULL);
        } else {
            lx->tok.kind = NMO_DSL_TOK_INT;
            lx->tok.val.i64 = strtoll(tmp, NULL, 10);
        }
        lx->cur = p;
        lx->col += (uint32_t)n;
        return;
    }

    /* @ */
    if (*s == '@') {
        lx->tok.kind = NMO_DSL_TOK_AT;
        lx->tok.start = s;
        lx->tok.len = 1;
        lx->tok.line = tok_line; lx->tok.col = tok_col; lx->tok.offset = tok_offset;
        lx->cur++;
        lx->col++;
        return;
    }

    /* identifier / keyword */
    if (is_ident_start(*s)) {
        const char *p = s + 1;
        while (is_ident_char(*p)) p++;
        size_t n = (size_t)(p - s);

        nmo_dsl_tok_kind_t kw = check_keyword(s, n);
        lx->tok.kind = kw;
        lx->tok.start = s;
        lx->tok.len = n;
        lx->tok.line = tok_line;
        lx->tok.col = tok_col;
        lx->tok.offset = tok_offset;
        lx->cur = p;
        lx->col += (uint32_t)n;
        return;
    }

    /* single-char tokens */
    lx->tok.start = s;
    lx->tok.len = 1;
    lx->tok.line = tok_line;
    lx->tok.col = tok_col;
    lx->tok.offset = tok_offset;
    lx->cur++;
    lx->col++;

    switch (*s) {
        case '(': lx->tok.kind = NMO_DSL_TOK_LPAREN; return;
        case ')': lx->tok.kind = NMO_DSL_TOK_RPAREN; return;
        case '.': lx->tok.kind = NMO_DSL_TOK_DOT; return;
        case ',': lx->tok.kind = NMO_DSL_TOK_COMMA; return;
        case '[': lx->tok.kind = NMO_DSL_TOK_LBRACK; return;
        case ']': lx->tok.kind = NMO_DSL_TOK_RBRACK; return;
        case ':': lx->tok.kind = NMO_DSL_TOK_COLON; return;
        case '?': lx->tok.kind = NMO_DSL_TOK_QUESTION; return;
        case '*': lx->tok.kind = NMO_DSL_TOK_STAR; return;
        case '+': lx->tok.kind = NMO_DSL_TOK_PLUS; return;
        case '-': lx->tok.kind = NMO_DSL_TOK_MINUS; return;
        case '/': lx->tok.kind = NMO_DSL_TOK_SLASH; return;
        case '%': lx->tok.kind = NMO_DSL_TOK_PERCENT; return;
        case '!': lx->tok.kind = NMO_DSL_TOK_BANG; return;
        case '<': lx->tok.kind = NMO_DSL_TOK_LT; return;
        case '>': lx->tok.kind = NMO_DSL_TOK_GT; return;
        case ';': lx->tok.kind = NMO_DSL_TOK_SEMICOLON; return;
        case '{': lx->tok.kind = NMO_DSL_TOK_LBRACE; return;
        case '}': lx->tok.kind = NMO_DSL_TOK_RBRACE; return;
        case '=': lx->tok.kind = NMO_DSL_TOK_EQ; return;
        default:
            (void)snprintf(lx->err, sizeof(lx->err), "unexpected character '%c'", *s);
            lx->tok.kind = NMO_DSL_TOK_ERROR;
            return;
    }
}
