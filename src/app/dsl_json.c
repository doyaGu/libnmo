/**
 * @file dsl_json.c
 * @brief Serialize DSL values to JSON via yyjson.
 */

#include "app/nmo_dsl_json.h"
#include "dsl/nmo_dsl.h"
#include "yyjson.h"

#include <stdio.h>
#include <string.h>

/* Format a DSL value into a short text representation for types that
 * do not have a direct JSON mapping (byref, object, type). */
static void format_opaque(const nmo_dsl_value_t *value, char *buf, size_t sz) {
    switch (value->kind) {
        case NMO_DSL_VALUE_BYREF:
            if (!value->as.byref.ptr) {
                snprintf(buf, sz, "null");
            } else {
                size_t bsz = value->as.byref.size;
                if (bsz == 0 && value->as.byref.type)
                    bsz = (size_t)value->as.byref.type->size;
                snprintf(buf, sz, "<byref:%zu bytes>", bsz);
            }
            break;
        case NMO_DSL_VALUE_OBJECT:
            snprintf(buf, sz, "<object:%p>", value->as.object.instance);
            break;
        case NMO_DSL_VALUE_TYPE:
            snprintf(buf, sz, "<type>");
            break;
        default:
            snprintf(buf, sz, "<unknown>");
            break;
    }
}

/* Add a value directly to a JSON array element (no key). */
static void add_to_array(const nmo_dsl_value_t *elem,
                         yyjson_mut_doc *doc,
                         yyjson_mut_val *arr) {
    switch (elem->kind) {
        case NMO_DSL_VALUE_NULL:
            yyjson_mut_arr_add_null(doc, arr);
            break;
        case NMO_DSL_VALUE_BOOL:
            yyjson_mut_arr_add_bool(doc, arr, elem->as.b);
            break;
        case NMO_DSL_VALUE_INT:
            yyjson_mut_arr_add_sint(doc, arr, elem->as.i);
            break;
        case NMO_DSL_VALUE_UINT:
            yyjson_mut_arr_add_uint(doc, arr, elem->as.u);
            break;
        case NMO_DSL_VALUE_REAL:
            yyjson_mut_arr_add_real(doc, arr, elem->as.r);
            break;
        case NMO_DSL_VALUE_STRING:
            yyjson_mut_arr_add_strcpy(doc, arr,
                                      elem->as.s ? elem->as.s : "");
            break;
        case NMO_DSL_VALUE_SEQ: {
            /* Recurse: build sub-array via nmo_dsl_value_to_json */
            yyjson_mut_val *sub = nmo_dsl_value_to_json(elem, doc, NULL, NULL);
            if (sub) yyjson_mut_arr_add_val(arr, sub);
            break;
        }
        default: {
            char buf[64];
            format_opaque(elem, buf, sizeof(buf));
            yyjson_mut_arr_add_strcpy(doc, arr, buf);
            break;
        }
    }
}

yyjson_mut_val *nmo_dsl_value_to_json(
    const nmo_dsl_value_t *value,
    yyjson_mut_doc *doc,
    yyjson_mut_val *parent,
    const char *key)
{
    if (!doc) return NULL;

    yyjson_mut_val *result = NULL;

    if (!value || value->kind == NMO_DSL_VALUE_NULL) {
        result = yyjson_mut_null(doc);
    } else {
        switch (value->kind) {
            case NMO_DSL_VALUE_BOOL:
                result = yyjson_mut_bool(doc, value->as.b);
                break;
            case NMO_DSL_VALUE_INT:
                result = yyjson_mut_sint(doc, value->as.i);
                break;
            case NMO_DSL_VALUE_UINT:
                result = yyjson_mut_uint(doc, value->as.u);
                break;
            case NMO_DSL_VALUE_REAL:
                result = yyjson_mut_real(doc, value->as.r);
                break;
            case NMO_DSL_VALUE_STRING:
                result = yyjson_mut_strcpy(doc,
                                           value->as.s ? value->as.s : "");
                break;
            case NMO_DSL_VALUE_SEQ: {
                yyjson_mut_val *arr = yyjson_mut_arr(doc);
                uint64_t count = nmo_dsl_seq_count(value->as.seq);
                for (uint64_t i = 0; i < count; i++) {
                    nmo_dsl_value_t elem = {0};
                    if (nmo_dsl_seq_get(value->as.seq, i, &elem)) {
                        add_to_array(&elem, doc, arr);
                    }
                }
                result = arr;
                break;
            }
            default: {
                /* byref, object, type -> text fallback */
                char buf[64];
                format_opaque(value, buf, sizeof(buf));
                result = yyjson_mut_strcpy(doc, buf);
                break;
            }
        }
    }

    if (!result) return NULL;

    /* Attach to parent if requested */
    if (parent && key) {
        yyjson_mut_obj_add_val(doc, parent, key, result);
    } else if (parent && !key) {
        yyjson_mut_arr_add_val(parent, result);
    }

    return result;
}
