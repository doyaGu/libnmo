/**
 * @file host.c
 * @brief What the host gives the engine: the files it reads, the registry and
 *        environment the TT blocks read, the current directory and the path
 *        manager's paths.
 *
 * Keys and names compare as Windows compares them, ignoring case (and a key's
 * trailing backslashes).
 */

#include "../engine_internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../platform/platform.h"

void eng_free_host(nmo_engine_t *engine)
{
    eng_clear_player_messages(engine);
    free(engine->host.messages.data);
    engine->host.messages.data = NULL;
    engine->host.messages.capacity = 0;
    free(engine->host.console.data);
    engine->host.console.data = NULL;
    engine->host.console.capacity = 0;
    for (size_t i = 0; i < engine->host.registry.count; i++) {
        free(engine->host.registry.data[i].section);
        free(engine->host.registry.data[i].name);
        free(engine->host.registry.data[i].value);
    }
    free(engine->host.registry.data);
    engine->host.registry.data = NULL;
    engine->host.registry.count = engine->host.registry.capacity = 0;
    for (size_t i = 0; i < engine->host.environment.count; i++) {
        free(engine->host.environment.data[i].name);
        free(engine->host.environment.data[i].value);
    }
    free(engine->host.environment.data);
    engine->host.environment.data = NULL;
    engine->host.environment.count = engine->host.environment.capacity = 0;
    free(engine->host.current_directory);
    engine->host.current_directory = NULL;
    for (int c = 0; c < ENG_PATH_CATEGORIES; c++) {
        for (size_t i = 0; i < engine->host.paths[c].count; i++) {
            free(engine->host.paths[c].data[i]);
        }
        free(engine->host.paths[c].data);
        engine->host.paths[c].data = NULL;
        engine->host.paths[c].count = engine->host.paths[c].capacity = 0;
    }
}

/* The whole file a name resolves to; NULL when it is not there. */
unsigned char *eng_read_file(nmo_engine_t *engine, const char *name, size_t *out_size)
{
    const char *path = engine->host.resolve_file != NULL
                           ? engine->host.resolve_file(engine->host.resolve_file_user_data, name)
                           : NULL;
    FILE *file = path != NULL ? nmo_platform_fopen(path, "rb") : NULL;
    if (file == NULL) {
        return NULL;
    }
    unsigned char *data = NULL;
    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (size > 0 && size < INT_MAX && fseek(file, 0, SEEK_SET) == 0) {
        data = (unsigned char *)malloc((size_t)size);
        if (data != NULL && fread(data, 1, (size_t)size, file) != (size_t)size) {
            free(data);
            data = NULL;
        }
    }
    fclose(file);
    *out_size = data != NULL ? (size_t)size : 0u;
    return data;
}

/*
 * CKPathManager::ResolveFileName: the name as it is, else its file name in
 * each path of the category, in order.
 */
unsigned char *eng_read_path_file(nmo_engine_t *engine, const char *name, int32_t category,
                                  size_t *out_size)
{
    unsigned char *data = eng_read_file(engine, name, out_size);
    if (data != NULL || category < 0 || category >= ENG_PATH_CATEGORIES) {
        return data;
    }
    const char *base = name;
    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    for (size_t i = 0; data == NULL && i < engine->host.paths[category].count; i++) {
        char *joined = eng_text_printf("%s\\%s", engine->host.paths[category].data[i], base);
        data = joined != NULL ? eng_read_file(engine, joined, out_size) : NULL;
        free(joined);
    }
    return data;
}

/* ============================================================================
 * Registry
 * ============================================================================ */

/* The length of a key without its trailing backslashes. */
static size_t section_length(const char *section)
{
    size_t length = strlen(section);
    while (length > 0 && section[length - 1] == '\\') {
        length--;
    }
    return length;
}

static bool same_text(const char *a, size_t a_length, const char *b, size_t b_length)
{
    if (a_length != b_length) {
        return false;
    }
    for (size_t i = 0; i < a_length; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

static eng_registry_value_t *registry_find(nmo_engine_t *engine, const char *section,
                                           const char *name)
{
    size_t length = section_length(section);
    for (size_t i = 0; i < engine->host.registry.count; i++) {
        eng_registry_value_t *entry = &engine->host.registry.data[i];
        if (same_text(entry->section, strlen(entry->section), section, length) &&
            same_text(entry->name, strlen(entry->name), name, strlen(name))) {
            return entry;
        }
    }
    return NULL;
}

nmo_status_t nmo_engine_set_registry_value(nmo_engine_t *engine, const char *section,
                                           const char *name, const char *value)
{
    if (engine == NULL || section == NULL || name == NULL || value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *copy = eng_text_copy(value, strlen(value));
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    eng_registry_value_t *entry = registry_find(engine, section, name);
    if (entry != NULL) {
        free(entry->value);
        entry->value = copy;
        return NMO_OK;
    }
    char *section_copy = eng_text_copy(section, section_length(section));
    char *name_copy = eng_text_copy(name, strlen(name));
    if (section_copy == NULL || name_copy == NULL || !ENG_VEC_PUSH(engine->host.registry, entry)) {
        free(copy);
        free(section_copy);
        free(name_copy);
        return NMO_ERR_NOMEM;
    }
    entry->section = section_copy;
    entry->name = name_copy;
    entry->value = copy;
    return NMO_OK;
}

const char *eng_registry_value(nmo_engine_t *engine, const char *section, const char *name)
{
    const eng_registry_value_t *entry =
        section != NULL && name != NULL ? registry_find(engine, section, name) : NULL;
    return entry != NULL ? entry->value : NULL;
}

const char *nmo_engine_registry_value(const nmo_engine_t *engine, const char *section,
                                     const char *name)
{
    return engine != NULL ? eng_registry_value((nmo_engine_t *)engine, section, name) : NULL;
}

bool eng_registry_has_key(nmo_engine_t *engine, const char *section)
{
    size_t length = section != NULL ? section_length(section) : 0;
    for (size_t i = 0; section != NULL && i < engine->host.registry.count; i++) {
        const char *key = engine->host.registry.data[i].section;
        size_t key_length = strlen(key);
        if (same_text(key, key_length < length ? key_length : length, section, length) &&
            (key_length == length || key[length] == '\\')) {
            return true;
        }
    }
    return false;
}

/* VxConfig reads a DWORD value; false when the value is not a number. */
bool eng_registry_int(nmo_engine_t *engine, const char *section, const char *name,
                      int32_t *out)
{
    const char *text = eng_registry_value(engine, section, name);
    char *end = NULL;
    long value = text != NULL ? strtol(text, &end, 0) : 0;
    if (text == NULL || end == text || *end != '\0') {
        return false;
    }
    *out = (int32_t)value;
    return true;
}

bool eng_registry_float(nmo_engine_t *engine, const char *section, const char *name,
                        float *out)
{
    const char *text = eng_registry_value(engine, section, name);
    char *end = NULL;
    float value = text != NULL ? strtof(text, &end) : 0.0f;
    if (text == NULL || end == text || *end != '\0') {
        return false;
    }
    *out = value;
    return true;
}

/* ============================================================================
 * Environment, current directory and paths
 * ============================================================================ */

eng_registry_value_t *eng_environment_find(nmo_engine_t *engine, const char *name)
{
    for (size_t i = 0; i < engine->host.environment.count; i++) {
        eng_registry_value_t *entry = &engine->host.environment.data[i];
        if (same_text(entry->name, strlen(entry->name), name, strlen(name))) {
            return entry;
        }
    }
    return NULL;
}

nmo_status_t nmo_engine_set_environment_variable(nmo_engine_t *engine, const char *name,
                                                 const char *value)
{
    if (engine == NULL || name == NULL || value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *copy = eng_text_copy(value, strlen(value));
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    eng_registry_value_t *entry = eng_environment_find(engine, name);
    if (entry != NULL) {
        free(entry->value);
        entry->value = copy;
        return NMO_OK;
    }
    char *name_copy = eng_text_copy(name, strlen(name));
    if (name_copy == NULL || !ENG_VEC_PUSH(engine->host.environment, entry)) {
        free(copy);
        free(name_copy);
        return NMO_ERR_NOMEM;
    }
    entry->name = name_copy;
    entry->value = copy;
    return NMO_OK;
}

nmo_status_t nmo_engine_set_current_directory(nmo_engine_t *engine, const char *path)
{
    if (engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *copy = path != NULL ? eng_text_copy(path, strlen(path)) : NULL;
    if (path != NULL && copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    free(engine->host.current_directory);
    engine->host.current_directory = copy;
    return NMO_OK;
}

nmo_status_t nmo_engine_add_path(nmo_engine_t *engine, int32_t category, const char *path)
{
    if (engine == NULL || path == NULL || category < 0 || category >= ENG_PATH_CATEGORIES) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *copy = eng_text_copy(path, strlen(path));
    char **slot = NULL;
    if (copy == NULL || !ENG_VEC_PUSH(engine->host.paths[category], slot)) {
        free(copy);
        return NMO_ERR_NOMEM;
    }
    *slot = copy;
    return NMO_OK;
}

const char *nmo_engine_path(const nmo_engine_t *engine, int32_t category, int32_t index)
{
    if (engine == NULL || category < 0 || category >= ENG_PATH_CATEGORIES || index < 0 ||
        (size_t)index >= engine->host.paths[category].count) {
        return NULL;
    }
    return engine->host.paths[category].data[index];
}

/* ============================================================================
 * The player's window
 * ============================================================================ */

void eng_post_player_message(nmo_engine_t *engine, uint32_t message, int32_t param, int32_t param2,
                             const char *text)
{
    char *copy = text != NULL ? eng_text_copy(text, strlen(text)) : NULL;
    nmo_engine_player_message_t *slot = NULL;
    if ((text != NULL && copy == NULL) || !ENG_VEC_PUSH(engine->host.messages, slot)) {
        free(copy);
        return;
    }
    *slot = (nmo_engine_player_message_t){message, param, param2, copy};
}

void eng_clear_player_messages(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->host.messages.count; i++) {
        free((char *)engine->host.messages.data[i].text);
    }
    engine->host.messages.count = 0;
    for (size_t i = 0; i < engine->host.console.count; i++) {
        free((char *)engine->host.console.data[i].text);
    }
    engine->host.console.count = 0;
}

void eng_console(nmo_engine_t *engine, const char *text, bool beep)
{
    char *copy = eng_text_copy(text != NULL ? text : "", text != NULL ? strlen(text) : 0);
    nmo_engine_console_line_t *slot = NULL;
    if (copy == NULL || !ENG_VEC_PUSH(engine->host.console, slot)) {
        free(copy);
        return;
    }
    *slot = (nmo_engine_console_line_t){copy, beep};
}

int32_t nmo_engine_console_line_count(const nmo_engine_t *engine)
{
    return engine != NULL ? (int32_t)engine->host.console.count : 0;
}

bool nmo_engine_console_line(const nmo_engine_t *engine, int32_t index, nmo_engine_console_line_t *out)
{
    if (engine == NULL || index < 0 || (size_t)index >= engine->host.console.count) {
        return false;
    }
    if (out != NULL) {
        *out = engine->host.console.data[index];
    }
    return true;
}

int32_t nmo_engine_player_message_count(const nmo_engine_t *engine)
{
    return engine != NULL ? (int32_t)engine->host.messages.count : 0;
}

bool nmo_engine_player_message(const nmo_engine_t *engine, int32_t index,
                               nmo_engine_player_message_t *out)
{
    if (engine == NULL || index < 0 || (size_t)index >= engine->host.messages.count) {
        return false;
    }
    if (out != NULL) {
        *out = engine->host.messages.data[index];
    }
    return true;
}

void nmo_engine_set_window_activated(nmo_engine_t *engine)
{
    if (engine != NULL) {
        engine->host.window_activated = true;
    }
}
