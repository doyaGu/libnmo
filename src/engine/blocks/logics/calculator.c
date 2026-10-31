/**
 * @file calculator.c
 * @brief Ports of the Logics/Calculator building blocks.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). Random numbers come from the engine's seeded generator instead of
 * rand().
 */

#include "../../engine_block_util.h"

#include "type/nmo_type_guids.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- Logics/Calculator ---- */

static int bb_op(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_guid_t guid = eng_block_operation_guid(b);
    nmo_status_t status = nmo_guid_is_null(guid) ? NMO_ERR_NOT_FOUND
                                                 : eng_block_run_operation(b, guid);
    eng_emit(engine, NMO_ENGINE_EVENT_OPERATION, b->node, 0, (int32_t)status);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_identity(blk_t *b)
{
    int count = nmo_engine_block_input_parameter_count(b);
    if (count != nmo_engine_block_output_parameter_count(b)) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the CK function throws */
    }
    for (int i = 0; i < count; i++) {
        nmo_engine_block_copy_input_to_output(b, i, i);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_random(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_guid_t guid = nmo_engine_block_output_parameter_type(b, 0);
    if (nmo_guid_equals(guid, CKPGUID_BOOL)) {
        set_out_int(b, 0, nmo_engine_block_random(b) < 0.5 ? 0 : 1);
        return NMO_ENGINE_BR_OK;
    }
    if (!nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), guid) ||
        !nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 1), guid)) {
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_guid_equals(guid, CKPGUID_INT)) {
        int32_t min = get_in_int(b, 0, 0), max = get_in_int(b, 1, 0);
        set_out_int(b, 0, min + (int32_t)(nmo_engine_block_random(b) * (double)(max - min)));
        return NMO_ENGINE_BR_OK;
    }
    /* Float and the float tuples: every component between its min and max */
    size_t count = 0;
    if (type_derives(b, guid, CKPGUID_FLOAT)) {
        count = 1;
    } else if (nmo_guid_equals(guid, CKPGUID_2DVECTOR)) {
        count = 2;
    } else if (nmo_guid_equals(guid, CKPGUID_VECTOR)) {
        count = 3;
    } else if (nmo_guid_equals(guid, CKPGUID_COLOR) || nmo_guid_equals(guid, CKPGUID_RECT)) {
        count = 4;
    }
    if (count == 0) {
        return NMO_ENGINE_BR_OK;
    }
    float min[4] = {0}, max[4] = {0}, res[4] = {0};
    nmo_engine_block_get_input_value(b, 0, min, count * sizeof(float));
    nmo_engine_block_get_input_value(b, 1, max, count * sizeof(float));
    for (size_t i = 0; i < count; i++) {
        res[i] = min[i] + (float)nmo_engine_block_random(b) * (max[i] - min[i]);
    }
    if (nmo_guid_equals(guid, CKPGUID_RECT)) { /* VxRect::Normalize */
        if (res[0] > res[2]) { float t = res[0]; res[0] = res[2]; res[2] = t; }
        if (res[1] > res[3]) { float t = res[1]; res[1] = res[3]; res[3] = t; }
    }
    nmo_engine_block_set_output_value(b, 0, res, count * sizeof(float));
    return NMO_ENGINE_BR_OK;
}

/* Per Second: each output takes its input times the frame time in seconds, float by float. */
static int bb_per_second(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float delta = nmo_engine_block_delta_time(b) * 0.001f;
    for (int i = 0; i < nmo_engine_block_output_parameter_count(b); i++) {
        size_t in_size = 0;
        const uint8_t *in = (const uint8_t *)nmo_engine_block_input_data(b, i, &in_size);
        size_t out_size = nmo_engine_block_output_parameter_size(b, i);
        size_t count = out_size / sizeof(float);
        if (in == NULL || count == 0 || count > 16 || in_size < count * sizeof(float)) {
            continue;
        }
        float values[16];
        for (size_t k = 0; k < count; k++) {
            memcpy(&values[k], in + k * sizeof(float), sizeof(float));
            values[k] *= delta;
        }
        nmo_engine_block_set_output_value(b, i, values, count * sizeof(float));
    }
    return NMO_ENGINE_BR_OK;
}

/* Get Delta Time: the frame time, in milliseconds (an integer before version 2). */
static int bb_get_delta_time(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    set_out_float(b, 0, nmo_engine_block_delta_time(b));
    return NMO_ENGINE_BR_OK;
}

static int bb_get_delta_time_old(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    set_out_int(b, 0, (int32_t)nmo_engine_block_delta_time(b));
    return NMO_ENGINE_BR_OK;
}

static int cb_get_delta_time(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_engine_block_set_function(b, nmo_engine_block_version(b) == 0x00020000u
                                             ? bb_get_delta_time
                                             : bb_get_delta_time_old);
    }
    return NMO_ENGINE_BR_OK;
}

/* Mini Calculator: a op b, the operator a Binary Operator or the first character of a string. */
static int bb_mini_calculator(blk_t *b)
{
    float a = get_in_float(b, 1, 0.0f), c = get_in_float(b, 2, 0.0f);
    int32_t op = 1; /* CKADD */
    if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_STRING)) {
        size_t size = 0;
        const char *text = (const char *)nmo_engine_block_input_data(b, 0, &size);
        char sign = text != NULL && size > 0 ? text[0] : '\0';
        op = sign == '+' ? 1 : sign == '-' ? 2 : sign == '*' ? 3 : sign == '/' ? 4 : 0;
    } else {
        op = get_in_int(b, 0, 1);
    }
    float x = 0.0f; /* CK2 leaves it unset for another operator */
    switch (op) {
    case 1: x = a + c; break;
    case 2: x = a - c; break;
    case 3: x = a * c; break;
    case 4: x = a / c; break;
    default: break;
    }
    set_out_float(b, 0, x);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Calculator: the arithmetic expression of the first input on the others, 'a'
 * the second input, 'b' the third, and so on (Calculator.cpp). The
 * expression is read as CK reads it: upper-cased, spaces removed, made into
 * a postfix node stack with the operators' precedences, then evaluated from
 * its top. A syntax error writes no output; an illegal operation writes what
 * the evaluation got to. CK reads an integer input's bytes as a float.
 */
enum {
    CALC_ADD = 1, CALC_SUB, CALC_MULT, CALC_DIV, CALC_MOD,
    CALC_POW, CALC_OPEN, CALC_CLOSE, CALC_SIN, CALC_COS,
    CALC_ABS, CALC_LN, CALC_EXP, CALC_ATAN, CALC_SQRT,
    CALC_ACOS, CALC_ASIN, CALC_XCOMP, CALC_YCOMP, CALC_ZCOMP,
    CALC_TAN
};

typedef struct calc_op {
    int token;
    const char *tag;
    size_t length;
    int precedence;
} calc_op_t;

/* op_signification, in its order (the first tag a string starts with wins) */
static const calc_op_t calc_ops[] = {
    {CALC_ADD, "+", 1, 2},       {CALC_SUB, "-", 1, 3},       {CALC_MULT, "*", 1, 4},
    {CALC_DIV, "/", 1, 5},       {CALC_OPEN, "(", 1, 0},      {CALC_CLOSE, ")", 1, 256},
    {CALC_SIN, "SIN(", 4, 0},    {CALC_COS, "COS(", 4, 0},    {CALC_TAN, "TAN(", 4, 0},
    {CALC_XCOMP, ".X", 2, 7},    {CALC_YCOMP, ".Y", 2, 7},    {CALC_ZCOMP, ".Z", 2, 7},
    {CALC_ABS, "ABS(", 4, 0},    {CALC_SQRT, "SQRT(", 5, 0},  {CALC_ASIN, "ASIN(", 5, 0},
    {CALC_ACOS, "ACOS(", 5, 0},  {CALC_ATAN, "ATAN(", 5, 0},  {CALC_POW, "^", 1, 6},
    {CALC_LN, "LN(", 3, 0},      {CALC_EXP, "EXP(", 4, 0},    {CALC_MOD, "\\", 1, 5},
};

enum { CALC_NODE_OP = 1, CALC_FLOAT, CALC_VECTOR, CALC_PIN_FLOAT, CALC_PIN_VECTOR };

typedef struct calc_node {
    int type;
    int op;   /* CALC_NODE_OP */
    int pin;  /* CALC_PIN_*: the input; -1 for one the block does not have */
    float v[3];
} calc_node_t;

/* The parse (CreateNodeStack's globals), its stacks sized for the expression */
typedef struct calc {
    blk_t *b;
    calc_node_t *nodes;
    int node_count;
    int *ops;
    int op_count;
    int parens;
    char *token;
    bool cross_fixed; /* version 2's corrected cross product */
} calc_t;

#define CALC_SUCCESS 0

#define CALC_CREATION_ERROR (-1)

#define CALC_RUNTIME_ERROR (-2)

static const calc_op_t *calc_get_op(const char *str)
{
    for (size_t i = 0; i < sizeof(calc_ops) / sizeof(calc_ops[0]); i++) {
        if (strncmp(str, calc_ops[i].tag, calc_ops[i].length) == 0) {
            return &calc_ops[i];
        }
    }
    return NULL;
}

static int calc_precedence(int token)
{
    for (size_t i = 0; i < sizeof(calc_ops) / sizeof(calc_ops[0]); i++) {
        if (calc_ops[i].token == token) {
            return calc_ops[i].precedence;
        }
    }
    return 0;
}

static void calc_push_op(calc_t *c, int op)
{
    if (calc_precedence(op) == 0) {
        c->parens++;
    }
    c->ops[c->op_count++] = op;
}

static void calc_push_value(calc_t *c, float value)
{
    calc_node_t *node = &c->nodes[c->node_count++];
    memset(node, 0, sizeof(*node));
    node->type = CALC_FLOAT;
    node->v[0] = value;
}

/* PushOpInNodeStack: the last operation into the node stack, but '(' */
static int calc_pop_op_to_nodes(calc_t *c)
{
    if (c->op_count == 0) {
        return CALC_CREATION_ERROR;
    }
    int op = c->ops[--c->op_count];
    if (op != CALC_OPEN) {
        calc_node_t *node = &c->nodes[c->node_count++];
        memset(node, 0, sizeof(*node));
        node->type = CALC_NODE_OP;
        node->op = op;
    }
    return op;
}

/* DoParenthesis: operations into the node stack down to the opening one */
static int calc_close_parenthesis(calc_t *c)
{
    if (1 > c->parens--) {
        return CALC_CREATION_ERROR;
    }
    int op;
    do {
        op = calc_pop_op_to_nodes(c);
        if (op < CALC_SUCCESS) {
            break;
        }
    } while (calc_precedence(op) != 0);
    return op;
}

/*
 * GetExpression: the operation `str` starts with, or the operand before the
 * next operation (a '-' starts a number, or an exponent's sign). A '-' that
 * negates a non-number reads as 0 minus it.
 */
static const char *calc_get_expression(calc_t *c, const char *str)
{
    if (strncmp(str, "PI", 2) == 0) {
        return "PI";
    }
    const char *ptr = str;
    char *out = c->token;
    while (*ptr) {
        const calc_op_t *op = calc_get_op(ptr);
        if (op != NULL) {
            if (*ptr == '-') {
                if (str != ptr && (ptr[-1] != 'E' || str == ptr - 1)) {
                    break;
                }
                if (str == ptr && !isdigit((unsigned char)ptr[1]) && ptr[1] != '.') {
                    calc_push_value(c, 0.0f);
                    return op->tag;
                }
            } else if (str == ptr) {
                return op->tag;
            } else {
                break;
            }
        }
        *out++ = *ptr++;
    }
    *out = '\0';
    return c->token;
}

/* CreateNodeStack on the upper-cased expression without spaces */
static int calc_parse(calc_t *c, const char *line)
{
    const char *ptr = line;
    int state = 0; /* 0 expecting an operand, 1 an operation */
    while (*ptr) {
        if (state == 0) {
            const char *str = calc_get_expression(c, ptr);
            const calc_op_t *op = calc_get_op(str);
            if (op != NULL && strlen(str) == op->length) {
                calc_push_op(c, op->token);
                ptr += op->length;
                continue;
            }
            if (strcmp(str, "-") == 0) {
                calc_push_op(c, CALC_SUB);
                ++ptr;
                continue;
            }
            calc_node_t *node = &c->nodes[c->node_count];
            memset(node, 0, sizeof(*node));
            if (str[1] == '\0' && str[0] >= 'A' && str[0] <= 'Z') {
                /* An input by its letter */
                int pin = str[0] - 'A' + 1;
                node->type = CALC_FLOAT;
                node->pin = -1;
                if (pin < nmo_engine_block_input_parameter_count(c->b)) {
                    nmo_guid_t type = nmo_engine_block_input_parameter_type(c->b, pin);
                    if (nmo_guid_equals(type, CKPGUID_INT) || nmo_guid_equals(type, CKPGUID_FLOAT) ||
                        nmo_guid_equals(type, CKPGUID_PERCENTAGE)) {
                        node->type = CALC_PIN_FLOAT;
                    } else if (nmo_guid_equals(type, CKPGUID_VECTOR)) {
                        node->type = CALC_PIN_VECTOR;
                    } else {
                        return CALC_CREATION_ERROR;
                    }
                    node->pin = pin;
                }
                c->node_count++;
                ++ptr;
                state = 1;
                continue;
            }
            node->type = CALC_FLOAT;
            if (strcmp(str, "PI") == 0) {
                node->v[0] = 3.1415926535f;
            } else {
                node->v[0] = (float)strtod(str, NULL);
                if (node->v[0] == 0.0f && strchr(str, '0') == NULL) {
                    return CALC_CREATION_ERROR;
                }
            }
            c->node_count++;
            ptr += strlen(str);
            state = 1;
        } else {
            const calc_op_t *op = calc_get_op(ptr);
            if (op == NULL) {
                return CALC_CREATION_ERROR;
            }
            if (*ptr == ')') {
                int status = calc_close_parenthesis(c);
                if (status < CALC_SUCCESS) {
                    return status;
                }
            } else {
                while (c->op_count > 0 && op->precedence <= calc_precedence(c->ops[c->op_count - 1])) {
                    int status = calc_pop_op_to_nodes(c);
                    if (status < CALC_SUCCESS) {
                        return status;
                    }
                }
                calc_push_op(c, op->token);
                if (op->token != CALC_XCOMP && op->token != CALC_YCOMP && op->token != CALC_ZCOMP) {
                    state = 0;
                }
            }
            ptr += op->length;
        }
    }
    while (c->op_count > 0) {
        int status = calc_pop_op_to_nodes(c);
        if (status < CALC_SUCCESS) {
            return status;
        }
    }
    return CALC_SUCCESS;
}

/* EvalNode: the node on top of the stack, popping it and what it operates on */
static int calc_eval(calc_t *c, float *val, int *type)
{
    if (c->node_count == 0) {
        return CALC_RUNTIME_ERROR;
    }
    const calc_node_t *node = &c->nodes[--c->node_count];
    switch (node->type) {
    case CALC_FLOAT:
        val[0] = node->v[0];
        *type = CALC_FLOAT;
        return CALC_SUCCESS;
    case CALC_PIN_FLOAT:
        nmo_engine_block_get_input_value(c->b, node->pin, val, sizeof(float));
        *type = CALC_FLOAT;
        return CALC_SUCCESS;
    case CALC_PIN_VECTOR:
        nmo_engine_block_get_input_value(c->b, node->pin, val, 3u * sizeof(float));
        *type = CALC_VECTOR;
        return CALC_SUCCESS;
    case CALC_NODE_OP:
        break;
    default:
        return CALC_RUNTIME_ERROR;
    }

    int op = node->op;
    *type = CALC_FLOAT;
    float a[3] = {0.0f, 0.0f, 0.0f}, l[3] = {0.0f, 0.0f, 0.0f}; /* right, then left operand */
    int ta = 0, tl = 0;
    if (op == CALC_OPEN || op == CALC_CLOSE || calc_eval(c, a, &ta) != CALC_SUCCESS) {
        return CALC_RUNTIME_ERROR;
    }
    bool binary = op == CALC_ADD || op == CALC_SUB || op == CALC_MULT || op == CALC_DIV ||
                  op == CALC_POW || op == CALC_MOD;
    if (binary && calc_eval(c, l, &tl) != CALC_SUCCESS) {
        return CALC_RUNTIME_ERROR;
    }
    switch (op) {
    case CALC_XCOMP:
    case CALC_YCOMP:
    case CALC_ZCOMP:
        if (ta != CALC_VECTOR) {
            return CALC_RUNTIME_ERROR;
        }
        val[0] = a[op - CALC_XCOMP];
        return CALC_SUCCESS;
    case CALC_ADD:
    case CALC_SUB:
        val[0] = op == CALC_ADD ? l[0] + a[0] : l[0] - a[0];
        if (ta != tl) {
            return CALC_RUNTIME_ERROR;
        }
        if (ta == CALC_VECTOR) {
            for (int k = 1; k < 3; k++) {
                val[k] = op == CALC_ADD ? l[k] + a[k] : l[k] - a[k];
            }
            *type = CALC_VECTOR;
        }
        return CALC_SUCCESS;
    case CALC_MULT:
        if (ta == tl) {
            /* a product, or vectors' dot product */
            val[0] = ta == CALC_FLOAT ? l[0] * a[0] : l[0] * a[0] + l[1] * a[1] + l[2] * a[2];
            return CALC_SUCCESS;
        }
        *type = CALC_VECTOR;
        for (int k = 0; k < 3; k++) {
            val[k] = ta == CALC_FLOAT ? a[0] * l[k] : l[0] * a[k];
        }
        return CALC_SUCCESS;
    case CALC_DIV:
        if (a[0] == 0.0f || (ta == CALC_VECTOR && (a[1] == 0.0f || a[2] == 0.0f))) {
            return CALC_RUNTIME_ERROR;
        }
        if (ta == tl) {
            /* a quotient, or the sum of the vectors' component quotients */
            val[0] = ta == CALC_FLOAT ? l[0] / a[0] : l[0] / a[0] + l[1] / a[1] + l[2] / a[2];
            return CALC_SUCCESS;
        }
        *type = CALC_VECTOR;
        for (int k = 0; k < 3; k++) {
            val[k] = ta == CALC_FLOAT ? l[k] / a[0] : l[0] / a[k];
        }
        return CALC_SUCCESS;
    case CALC_POW:
        if (ta == CALC_VECTOR && tl == CALC_VECTOR) {
            /* the cross product: left ^ right since version 2, the other way before */
            const float *p = c->cross_fixed ? l : a, *q = c->cross_fixed ? a : l;
            val[0] = p[1] * q[2] - q[1] * p[2];
            val[1] = q[0] * p[2] - p[0] * q[2];
            val[2] = p[0] * q[1] - q[0] * p[1];
            *type = CALC_VECTOR;
            return CALC_SUCCESS;
        }
        val[0] = powf(l[0], a[0]);
        return CALC_SUCCESS;
    case CALC_MOD:
        if (a[0] == 0.0f) {
            return CALC_RUNTIME_ERROR;
        }
        val[0] = fmodf(l[0], a[0]);
        return CALC_SUCCESS;
    case CALC_ABS:
        val[0] = fabsf(a[0]);
        if (ta == CALC_VECTOR) {
            val[1] = fabsf(a[1]);
            val[2] = fabsf(a[2]);
            *type = CALC_VECTOR;
        }
        return CALC_SUCCESS;
    case CALC_SIN: val[0] = sinf(a[0]); return CALC_SUCCESS;
    case CALC_COS: val[0] = cosf(a[0]); return CALC_SUCCESS;
    case CALC_TAN: val[0] = tanf(a[0]); return CALC_SUCCESS;
    case CALC_ATAN: val[0] = atanf(a[0]); return CALC_SUCCESS;
    case CALC_EXP: val[0] = expf(a[0]); return CALC_SUCCESS;
    case CALC_SQRT:
        if (a[0] < 0.0f) {
            return CALC_RUNTIME_ERROR;
        }
        val[0] = sqrtf(a[0]);
        return CALC_SUCCESS;
    case CALC_ASIN:
    case CALC_ACOS:
        if (a[0] > 1.0f || a[0] < -1.0f) {
            return CALC_RUNTIME_ERROR;
        }
        val[0] = op == CALC_ASIN ? asinf(a[0]) : acosf(a[0]);
        return CALC_SUCCESS;
    case CALC_LN:
        if (a[0] <= 0.0f) {
            return CALC_RUNTIME_ERROR;
        }
        val[0] = logf(a[0]);
        return CALC_SUCCESS;
    default:
        return CALC_RUNTIME_ERROR;
    }
}

static int bb_calculator(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    size_t size = 0;
    const char *expression = (const char *)nmo_engine_block_input_data(b, 0, &size);
    size_t length = expression != NULL ? strnlen(expression, size) : 0u;
    /* Each character makes at most an operation and two nodes (a '-' its 0) */
    calc_t c;
    memset(&c, 0, sizeof(c));
    c.b = b;
    c.cross_fixed = nmo_engine_block_version(b) >= 0x00020000u;
    char *line = (char *)malloc(length + 1u);
    c.token = (char *)malloc(length + 1u);
    c.nodes = (calc_node_t *)malloc((2u * length + 1u) * sizeof(calc_node_t));
    c.ops = (int *)malloc((length + 1u) * sizeof(int));
    if (line == NULL || c.token == NULL || c.nodes == NULL || c.ops == NULL) {
        free(line);
        free(c.token);
        free(c.nodes);
        free(c.ops);
        return NMO_ENGINE_BR_GENERICERROR;
    }
    size_t n = 0;
    for (size_t i = 0; i < length; i++) {
        if (!isspace((unsigned char)expression[i])) {
            line[n++] = (char)toupper((unsigned char)expression[i]);
        }
    }
    line[n] = '\0';
    if (calc_parse(&c, line) == CALC_SUCCESS && c.node_count > 0) {
        float x[3] = {0.0f, 0.0f, 0.0f};
        int type = 0;
        calc_eval(&c, x, &type); /* "Illegal Operation In CALCULATOR": x as it got */
        bool vector = nmo_guid_equals(nmo_engine_block_output_parameter_type(b, 0), CKPGUID_VECTOR);
        nmo_engine_block_set_output_value(b, 0, x, vector ? sizeof(x) : sizeof(x[0]));
    }
    free(line);
    free(c.token);
    free(c.nodes);
    free(c.ops);
    return NMO_ENGINE_BR_OK;
}

/*
 * Get Component: the components of a Color, Vector, Euler Angles or 2D
 * Vector, and a Rectangle or Box as corners, minimum and size, or center and
 * half size by its "Rectangle/Box Mode" setting.
 */
static int bb_get_component(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_guid_t type = nmo_engine_block_input_parameter_type(b, 0);
    float v[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, v, sizeof(v));
    int components = 0;
    if (nmo_guid_equals(type, CKPGUID_COLOR)) {
        components = 4;
    } else if (nmo_guid_equals(type, CKPGUID_VECTOR) || nmo_guid_equals(type, CKPGUID_EULERANGLES)) {
        components = 3;
    } else if (nmo_guid_equals(type, CKPGUID_2DVECTOR)) {
        components = 2;
    }
    for (int i = 0; i < components; i++) {
        set_out_float(b, i, v[i]);
    }
    int32_t mode = 1;
    nmo_engine_block_get_local_value(b, 0, &mode, sizeof(mode));
    if (nmo_guid_equals(type, CKPGUID_RECT)) {
        /* left, top, right, bottom */
        float corner[2] = {v[0], v[1]}, other[2] = {v[2], v[3]};
        if (mode == 2) {
            other[0] = v[2] - v[0];
            other[1] = v[3] - v[1];
        } else if (mode == 3) {
            other[0] = (v[2] - v[0]) * 0.5f;
            other[1] = (v[3] - v[1]) * 0.5f;
            corner[0] = (v[0] + v[2]) * 0.5f;
            corner[1] = (v[1] + v[3]) * 0.5f;
        } else if (mode != 1) {
            corner[0] = corner[1] = other[0] = other[1] = 0.0f;
        }
        nmo_engine_block_set_output_value(b, 0, corner, sizeof(corner));
        nmo_engine_block_set_output_value(b, 1, other, sizeof(other));
    } else if (nmo_guid_equals(type, CKPGUID_BOX)) {
        /* VxBbox: Max, then Min */
        float first[3] = {v[3], v[4], v[5]}, second[3] = {v[0], v[1], v[2]};
        for (int k = 0; k < 3; k++) {
            if (mode == 2) {
                second[k] = v[k] - v[3 + k];
            } else if (mode == 3) {
                first[k] = (v[k] + v[3 + k]) * 0.5f;
                second[k] = (v[k] - v[3 + k]) * 0.5f;
            }
        }
        nmo_engine_block_set_output_value(b, 0, first, sizeof(first));
        nmo_engine_block_set_output_value(b, 1, second, sizeof(second));
    }
    return NMO_ENGINE_BR_OK;
}

/* A block's local parameter `pos` */
static eng_param_t *block_local(blk_t *b, int pos)
{
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    return eng_node_param(b->engine, node->first_local, node->local_count, pos);
}

/*
 * Variation: X less its value at the last In (kept in local 0), and Change
 * or No Change. Reset stores Reset Value as that value, without an output.
 */
static int bb_variation(blk_t *b)
{
    eng_param_t *old = block_local(b, 0);
    if (!IN_ACTIVE(b, 0)) {
        IN_OFF(b, 1);
        if (old != NULL && old->size > 0) {
            uint8_t *value = (uint8_t *)calloc(1, old->size);
            if (value == NULL) {
                return NMO_ENGINE_BR_GENERICERROR;
            }
            nmo_engine_block_get_input_value(b, 1, value, old->size);
            eng_param_write(b->engine, old, value, old->size);
            free(value);
        }
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    nmo_guid_t type = nmo_engine_block_output_parameter_type(b, 0);
    int floats = 0;
    bool old_on_change = true; /* the float case gives OldX on a change, the vectors on none */
    if (nmo_guid_equals(type, CKPGUID_FLOAT) || nmo_guid_equals(type, CKPGUID_PERCENTAGE) ||
        nmo_guid_equals(type, CKPGUID_ANGLE)) {
        floats = 1;
    } else if (nmo_guid_equals(type, CKPGUID_VECTOR)) {
        floats = 3;
        old_on_change = false;
    } else if (nmo_guid_equals(type, CKPGUID_2DVECTOR)) {
        floats = 2;
        old_on_change = false;
    } else if (nmo_guid_equals(type, CKPGUID_COLOR)) {
        floats = 4;
        old_on_change = false;
    }
    if (floats > 0) {
        float x[4] = {0}, previous[4] = {0};
        nmo_engine_block_get_input_value(b, 0, x, (size_t)floats * sizeof(float));
        nmo_engine_block_get_local_value(b, 0, previous, (size_t)floats * sizeof(float));
        nmo_engine_block_set_local_value(b, 0, x, (size_t)floats * sizeof(float));
        bool changed = false;
        for (int i = 0; i < floats; i++) {
            x[i] -= previous[i];
            changed = changed || x[i] != 0.0f;
        }
        nmo_engine_block_set_output_value(b, 0, x, (size_t)floats * sizeof(float));
        if (changed == old_on_change) {
            nmo_engine_block_set_output_value(b, 1, previous, (size_t)floats * sizeof(float));
        }
        OUT_ON(b, changed ? 0 : 1);
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_guid_equals(type, CKPGUID_INT)) {
        int32_t x = get_in_int(b, 0, 0), previous = 0;
        nmo_engine_block_get_local_value(b, 0, &previous, sizeof(previous));
        nmo_engine_block_set_local_value(b, 0, &x, sizeof(x));
        x -= previous;
        set_out_int(b, 0, x);
        if (x != 0) {
            set_out_int(b, 1, previous);
        }
        OUT_ON(b, x != 0 ? 0 : 1);
        return NMO_ENGINE_BR_OK;
    }
    /* Any other type: a change of its bytes, the old value into OldX */
    size_t size = 0;
    const uint8_t *now = (const uint8_t *)nmo_engine_block_input_data(b, 0, &size);
    if (now == NULL || old == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    size_t compared = size < old->size ? size : old->size;
    if (compared > 0 && memcmp(now, old->data, compared) != 0) {
        size_t out_size = nmo_engine_block_output_parameter_size(b, 1);
        size_t copied = out_size < old->size ? out_size : old->size;
        if (copied > 0) {
            nmo_engine_block_set_output_value(b, 1, old->data, copied);
        }
        OUT_ON(b, 0);
    } else {
        OUT_ON(b, 1);
    }
    eng_param_write(b->engine, old, now, size);
    return NMO_ENGINE_BR_OK;
}

/* Bezier Transform: X through the curve, between the boundaries (a Rectangle, or 4 floats). */
static int bb_bezier_transform(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float x = get_in_float(b, 0, 0.0f);
    size_t curve_size = 0;
    const void *curve = nmo_engine_block_input_data(b, 1, &curve_size);
    if (curve == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    float rect[4] = {1.0f, 0.0f, 0.0f, 1.0f}; /* left, top, right, bottom */
    int32_t four_floats = 0;
    nmo_engine_block_get_local_value(b, 0, &four_floats, sizeof(four_floats));
    if (four_floats) {
        rect[0] = get_in_float(b, 2, rect[0]);
        rect[2] = get_in_float(b, 3, rect[2]);
        rect[3] = get_in_float(b, 4, rect[3]);
        rect[1] = get_in_float(b, 5, rect[1]);
    } else {
        nmo_engine_block_get_input_value(b, 2, rect, sizeof(rect));
    }
    float y = rect[3] + (rect[1] - rect[3]) *
                           nmo_engine_curve_y(curve, curve_size, (x - rect[0]) / (rect[2] - rect[0]));
    set_out_float(b, 0, y);
    return NMO_ENGINE_BR_OK;
}

/* Threshold: X against MIN and MAX; the output is X clamped. */
static int bb_threshold(blk_t *b)
{
    IN_OFF(b, 0);
    float x = get_in_float(b, 0, 0.0f);
    float min = get_in_float(b, 1, 0.0f);
    float clamped = x;
    if (x < min) {
        clamped = min;
        OUT_ON(b, 0);
    } else {
        /* MAX is read only now, as the original reads it (an input may run an operation) */
        float max = get_in_float(b, 2, 0.0f);
        if (x > max) {
            clamped = max;
            OUT_ON(b, 1);
        } else {
            OUT_ON(b, 2);
        }
    }
    set_out_float(b, 0, clamped);
    return NMO_ENGINE_BR_OK;
}

/* ---- Logics/Calculator ---- */

/*
 * Set Component: the output (color, vector, 2D vector, rectangle or box) made
 * of the inputs. The struct types it also fills (CKStructHelper) are left out.
 */
static int bb_set_component(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_guid_t out = nmo_engine_block_output_parameter_type(b, 0);
    if (nmo_guid_is_null(out)) {
        return NMO_ENGINE_BR_OK;
    }
    float v[6] = {0};
    if (nmo_guid_equals(out, CKPGUID_COLOR)) {
        for (int i = 0; i < 4; i++) {
            v[i] = get_in_float(b, i, 0.0f);
        }
        nmo_engine_block_set_output_value(b, 0, v, 4 * sizeof(float));
    } else if (nmo_guid_equals(out, CKPGUID_VECTOR) || nmo_guid_equals(out, CKPGUID_EULERANGLES) ||
               nmo_guid_equals(out, CKPGUID_2DVECTOR)) {
        int count = nmo_guid_equals(out, CKPGUID_2DVECTOR) ? 2 : 3;
        for (int i = 0; i < count; i++) {
            v[i] = get_in_float(b, i, 0.0f);
        }
        nmo_engine_block_set_output_value(b, 0, v, (size_t)count * sizeof(float));
    } else if (nmo_guid_equals(out, CKPGUID_RECT) || nmo_guid_equals(out, CKPGUID_BOX)) {
        /* Two points as corners (1), position and size (2), or center and half size (3) */
        size_t n = nmo_guid_equals(out, CKPGUID_RECT) ? 2 : 3;
        float p1[3] = {0}, p2[3] = {0};
        nmo_engine_block_get_input_value(b, 0, p1, n * sizeof(float));
        nmo_engine_block_get_input_value(b, 1, p2, n * sizeof(float));
        int32_t mode = 1;
        nmo_engine_block_get_local_value(b, 0, &mode, sizeof(mode));
        float lo[3], hi[3];
        for (size_t i = 0; i < n; i++) {
            lo[i] = mode == 3 ? p1[i] - p2[i] : p1[i];
            hi[i] = mode == 2 ? p1[i] + p2[i] : mode == 3 ? p1[i] + p2[i] : p2[i];
        }
        if (mode < 1 || mode > 3) {
            return NMO_ENGINE_BR_OK; /* the CK switch leaves the value as it was */
        }
        /* VxRect is left, top, right, bottom; VxBbox is Min, then Max */
        memcpy(v, lo, n * sizeof(float));
        memcpy(v + n, hi, n * sizeof(float));
        nmo_engine_block_set_output_value(b, 0, v, 2 * n * sizeof(float));
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_logics_calculator_blocks[] = {
    {NMO_GUID(0x2d5d6d01, 0x6a353eb0), "Op", bb_op, NULL, NULL},
    {NMO_GUID(0x15151652, 0xaeefffd5), "Identity", bb_identity, NULL, NULL},
    {NMO_GUID(0x0c622386, 0x1c3054f7), "Random", bb_random, NULL, NULL},
    {NMO_GUID(0x448e54ce, 0x75a655c5), "Per Second", bb_per_second, NULL, NULL},
    {NMO_GUID(0x47dc3232, 0x64bf203a), "Get Delta Time", bb_get_delta_time, cb_get_delta_time,
     NULL},
    {NMO_GUID(0x55bc3115, 0x1dfe1e40), "Mini Calculator", bb_mini_calculator, NULL, NULL},
    {NMO_GUID(0x4bc209b8, 0x5b3679b0), "Calculator", bb_calculator, NULL, NULL},
    {NMO_GUID(0x2689057b, 0x1a1a446a), "Get Component", bb_get_component, NULL, NULL},
    {NMO_GUID(0x3e7335d9, 0x29687f36), "Variation", bb_variation, NULL, NULL},
    {NMO_GUID(0x6d433e1b, 0x191954b4), "Bezier Transform", bb_bezier_transform, NULL, NULL},
    {NMO_GUID(0x655e6af4, 0x08c0596f), "Threshold", bb_threshold, NULL, NULL},
    {NMO_GUID(0x6e800755, 0x57b64acb), "Set Component", bb_set_component, NULL, NULL},
};

nmo_status_t eng_register_logics_calculator(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_calculator_blocks);
}
