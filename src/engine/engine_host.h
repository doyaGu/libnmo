/**
 * @file engine_host.h
 * @brief What the engine takes from its host: the files it reads and writes,
 *        the registry and environment, the current directory and the path
 *        manager's paths, and the host's side of quitting.
 */

#ifndef NMO_ENGINE_HOST_H
#define NMO_ENGINE_HOST_H

#include "engine_base.h"

#define ENG_PATH_CATEGORIES 3

/* A registry value: `name` of key `section`, as text (an environment variable has no section). */
typedef struct eng_registry_value {
    char *section;
    char *name;
    char *value;
} eng_registry_value_t;

typedef struct eng_host {
    nmo_engine_resolve_file_fn resolve_file;
    void *resolve_file_user_data;
    nmo_engine_write_file_fn write_file;
    void *write_file_user_data;
    nmo_engine_file_system_t files; /* the TT file blocks'; zeroed for none */
    ENG_VEC(eng_registry_value_t) registry;    /* what the TT blocks read */
    ENG_VEC(eng_registry_value_t) environment; /* environment variables */
    char *current_directory;
    ENG_VEC(char *) paths[ENG_PATH_CATEGORIES]; /* CKPathManager: bitmap, data and sound paths */
    bool exit_requested; /* TT Exit to System posted TT_PLAYER_COMMAND_EXIT_TO_SYSTEM */
    bool window_activated; /* the player saw WM_ACTIVATE (InterfaceManager's flag) */
    ENG_VEC(nmo_engine_player_message_t) messages; /* posted this frame; texts are owned */
    ENG_VEC(nmo_engine_console_line_t) console;    /* written this frame; texts are owned */
} eng_host_t;

/* core/host.c */
void eng_free_host(nmo_engine_t *engine);
/* The whole file a name resolves to; NULL when it is not there. */
unsigned char *eng_read_file(nmo_engine_t *engine, const char *name, size_t *out_size);
/* The file a name resolves to through the path manager's paths of `category` (0 bitmaps) */
unsigned char *eng_read_path_file(nmo_engine_t *engine, const char *name, int32_t category,
                                  size_t *out_size);
/* A registry value as text; NULL when the host set none */
const char *eng_registry_value(nmo_engine_t *engine, const char *section, const char *name);
/* VxConfig reads a DWORD value; false when the value is not a number. */
bool eng_registry_int(nmo_engine_t *engine, const char *section, const char *name, int32_t *out);
bool eng_registry_float(nmo_engine_t *engine, const char *section, const char *name, float *out);
/* RegOpenKeyEx: a value has that key or a key under it */
bool eng_registry_has_key(nmo_engine_t *engine, const char *section);
/* PostMessage to the player's window; `text` is copied */
void eng_post_player_message(nmo_engine_t *engine, uint32_t message, int32_t param, int32_t param2,
                             const char *text);
/* The messages and console lines of the frame before go */
void eng_clear_player_messages(nmo_engine_t *engine);
/* CKContext::OutputToConsole(Ex(Beep)): a console line; `text` is copied */
void eng_console(nmo_engine_t *engine, const char *text, bool beep);
/* The environment variable of that name; NULL when the host set none */
eng_registry_value_t *eng_environment_find(nmo_engine_t *engine, const char *name);

#endif /* NMO_ENGINE_HOST_H */
