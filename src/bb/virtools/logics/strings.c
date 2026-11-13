/**
 * @file strings.c
 * @brief Ports of the Logics/Strings building blocks.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ).
 */

#include "../virtools.h"

#include "type/nmo_type_guids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../platform/platform.h"

/* ============================================================================
 * Logics/Strings
 * ============================================================================ */

static void set_out_string(blk_t *b, int pos, const char *text)
{
    nmo_engine_block_set_output_value(b, pos, text, strlen(text) + 1u);
}

/*
 * The single conversion of a Create String format, when it fits the value:
 * an integer one for an Integer, a floating one for a Float, %s for text.
 * CK2 hands any format to sprintf; one that does not fit is not used here.
 */
static char format_conversion(const char *format)
{
    char conversion = 0;
    for (const char *p = format; *p != '\0'; p++) {
        if (*p != '%') {
            continue;
        }
        if (p[1] == '%') {
            p++;
            continue;
        }
        if (conversion != 0) {
            return 0;
        }
        p++;
        while (*p != '\0' && strchr("-+ #0123456789.", *p) != NULL) {
            p++;
        }
        while (*p == 'l' || *p == 'h') {
            p++;
        }
        if (*p == '\0' || strchr("diouxXcfeEgGs", *p) == NULL) {
            return 0;
        }
        conversion = *p;
    }
    return conversion;
}

/*
 * An element of Create String: input `pos` with its format (the local before
 * it), its source's operation run first, whatever its type (GetValue).
 */
static char *create_string_element(blk_t *b, int pos)
{
    eng_param_t *source = input_source(b, pos, true);
    if (source == NULL) {
        return NULL;
    }
    const eng_node_t *node = block_node(b);
    eng_param_t *local = eng_node_param(b->engine, node->first_local, node->local_count, pos - 1);
    const char *format = local != NULL && local->data != NULL && local->size > 0 &&
                                 memchr(local->data, '\0', local->size) != NULL
                             ? (const char *)local->data
                             : NULL;
    if (format == NULL || format[0] == '\0') {
        return eng_param_text(b->engine, source);
    }
    char conversion = format_conversion(format);
    if (nmo_guid_equals(source->type, CKPGUID_INT)) {
        if (conversion != 0 && strchr("diouxXc", conversion) != NULL) {
            return eng_text_printf(format, eng_param_read_int(source));
        }
        return eng_param_text(b->engine, source);
    }
    if (nmo_guid_equals(source->type, CKPGUID_FLOAT)) {
        float value = 0.0f;
        eng_param_read_floats(source, &value, 1);
        if (conversion != 0 && strchr("feEgG", conversion) != NULL) {
            return eng_text_printf(format, (double)value);
        }
        return eng_param_text(b->engine, source);
    }
    char *text = eng_param_text(b->engine, source);
    if (text == NULL || format[strlen(format) - 1] != 's' || conversion != 's') {
        return text;
    }
    char *formatted = eng_text_printf(format, text);
    free(text);
    return formatted;
}

/* Create String: the inputs after Delimiter as text, joined by it; Add Line appends a line. */
static int bb_create_string(blk_t *b)
{
    if (nmo_engine_block_version(b) < 0x00020000u) {
        OUT_ON(b, 0);
    }
    const char *delimiter = input_string(b, 0);
    if (delimiter == NULL) {
        delimiter = "";
    }
    if (IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        OUT_ON(b, 2);
        set_out_string(b, 0, "");
        if (!IN_ACTIVE(b, 0) && !IN_ACTIVE(b, 1)) {
            return NMO_ENGINE_BR_OK;
        }
    }

    size_t length = 0, capacity = 1;
    char *joined = (char *)calloc(1, capacity);
    int count = nmo_engine_block_input_parameter_count(b);
    for (int i = 1; joined != NULL && i < count; i++) {
        if (input_source(b, i, false) == NULL) {
            continue;
        }
        char *element = create_string_element(b, i);
        size_t add = (i != 1 ? strlen(delimiter) : 0u) + (element != NULL ? strlen(element) : 0u);
        if (length + add + 1u > capacity) {
            capacity = (length + add + 1u) * 2u;
            char *grown = (char *)realloc(joined, capacity);
            if (grown == NULL) {
                free(joined);
                joined = NULL;
                free(element);
                break;
            }
            joined = grown;
        }
        if (i != 1) {
            strcpy(joined + length, delimiter);
            length += strlen(delimiter);
        }
        if (element != NULL) {
            strcpy(joined + length, element);
            length += strlen(element);
        }
        free(element);
    }
    if (joined == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }

    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        set_out_string(b, 0, joined);
    } else if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        eng_param_t *out = block_output(b, 0);
        const char *current = out != NULL && out->data != NULL && out->size > 0 &&
                                      memchr(out->data, '\0', out->size) != NULL
                                  ? (const char *)out->data
                                  : "";
        /* Without format locals (the old version), or after text, a new line first */
        char *text = nmo_engine_block_local_parameter_count(b) == 0 || current[0] != '\0'
                         ? eng_text_printf("%s\n%s", current, joined)
                         : eng_text_printf("%s", joined);
        if (text != NULL) {
            set_out_string(b, 0, text);
            free(text);
        }
    }
    free(joined);
    return NMO_ENGINE_BR_OK;
}

/* Get SubString (CutString): Length characters from Start; Length 0 to the end. */
static int bb_get_substring(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    const char *string = input_string(b, 0);
    if (string == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    int len = (int)strlen(string);
    int start = 1, cut = 0;
    nmo_engine_block_get_input_value(b, 1, &start, sizeof(start));
    nmo_engine_block_get_input_value(b, 2, &cut, sizeof(cut));
    if (cut == 0 || start + cut > len) {
        cut = len - start;
    }
    if (start < 0) {
        cut += start;
        start = 0;
    }
    char *result = cut <= 0 || start >= len ? eng_text_copy("", 0)
                                            : eng_text_copy(string + start, (size_t)cut);
    if (result != NULL) {
        set_out_string(b, 0, result);
        free(result);
    }
    return NMO_ENGINE_BR_OK;
}

/* Scan String (SplitString): the elements between delimiters into the outputs after the count. */
static int bb_scan_string(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    const char *input = input_string(b, 0);
    if (input == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    char *string = eng_text_copy(input, strlen(input)); /* an output may be the input */
    const char *delimiter = input_string(b, 1);
    if (delimiter == NULL || delimiter[0] == '\0') {
        delimiter = " ";
    }
    size_t delimiter_size = strlen(delimiter);
    if (string == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }

    int32_t elements = 0;
    char *str = string;
    for (char *del; (del = strstr(str, delimiter)) != NULL; str = del + delimiter_size) {
        if (del == str) {
            continue; /* a delimiter at the start: skipped */
        }
        elements++;
        eng_param_t *out = block_output(b, elements);
        if (out == NULL) {
            break;
        }
        *del = '\0';
        eng_param_set_text(b->engine, out, str);
        *del = delimiter[0];
    }
    if (str[0] != '\0') {
        elements++;
        eng_param_t *out = block_output(b, elements);
        if (out == NULL) {
            free(string);
            return NMO_ENGINE_BR_OK;
        }
        eng_param_set_text(b->engine, out, str);
    }
    nmo_engine_block_set_output_value(b, 0, &elements, sizeof(elements));
    free(string);
    return NMO_ENGINE_BR_OK;
}

/* Reverse String: reverses the string its input reads, where it is. */
static int bb_reverse_string(blk_t *b)
{
    IN_OFF(b, 0);
    eng_param_t *source = input_source(b, 0, true);
    if (source != NULL && source->data != NULL && source->size > 0 &&
        memchr(source->data, '\0', source->size) != NULL) {
        char *string = (char *)source->data;
        size_t len = strlen(string);
        for (size_t i = 0; i < len / 2; i++) {
            char c = string[i];
            string[i] = string[len - 1 - i];
            string[len - 1 - i] = c;
        }
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Load String: the text of a file, as read in text mode on Windows (CR LF
 * becomes LF), or File Error.
 */
static int bb_load_string(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    const char *name = input_string(b, 0);
    const char *path = name != NULL && engine->host.resolve_file != NULL
                           ? engine->host.resolve_file(engine->host.resolve_file_user_data, name)
                           : NULL;
    FILE *file = path != NULL ? nmo_platform_fopen(path, "rb") : NULL;
    if (file == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    OUT_ON(b, 0);
    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : 0;
    char *text = size >= 0 && fseek(file, 0, SEEK_SET) == 0 ? (char *)malloc((size_t)size + 1u)
                                                              : NULL;
    size_t length = 0;
    if (text != NULL) {
        size_t read = fread(text, 1, (size_t)size, file);
        for (size_t i = 0; i < read; i++) {
            if (!(text[i] == '\r' && i + 1 < read && text[i + 1] == '\n')) {
                text[length++] = text[i];
            }
        }
        text[length] = '\0';
        nmo_engine_block_set_output_text(b, 0, text);
    }
    free(text);
    fclose(file);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_logics_strings_blocks[] = {
    {NMO_GUID_INIT(0x4bcd2f9d, 0x382652e2), "Create String", bb_create_string, NULL, NULL},
    {NMO_GUID_INIT(0x10b16051, 0x26173b39), "Get SubString", bb_get_substring, NULL, NULL},
    {NMO_GUID_INIT(0x4afa2f11, 0x034872e2), "Scan String", bb_scan_string, NULL, NULL},
    {NMO_GUID_INIT(0x72b17c2e, 0x644e2563), "Reverse String", bb_reverse_string, NULL, NULL},
    {NMO_GUID_INIT(0x391555d6, 0x42f2500e), "Load String", bb_load_string, NULL, NULL},
};

nmo_status_t eng_register_logics_strings(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_strings_blocks);
}
