/**
 * @file interface_manager.c
 * @brief Ports of the TT Interface Manager building blocks.
 *
 * Each function does what the block's function in TT_InterfaceManager_RT.dll
 * does (the reference is the Ballanced source of the blocks, or the DLL where
 * they differ). The machine is a fixed one: one render driver with 4:3 screen
 * modes in 16 and 32 bits, not in interface mode. The registry holds what the
 * host set and what the registry blocks wrote, as text. What the DLL posts to
 * the player's window goes to the host as player messages; its console and
 * message boxes are not kept. The game info and the installed arrays live in
 * the TT managers' state.
 */

#include "../bb_sdk.h"
#include "terratools.h"

#include "object/nmo_object_enum_defs.h"
#include "type/nmo_type_guids.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The registry value name of a row: its index. */
static char *row_entry(int32_t row)
{
    int length = snprintf(NULL, 0, "%d", (int)row);
    char *entry = length > 0 ? (char *)malloc((size_t)length + 1u) : NULL;
    if (entry != NULL) {
        snprintf(entry, (size_t)length + 1u, "%d", (int)row);
    }
    return entry;
}

/*
 * SaveArray-Mode of TT_ReadRegistry: each cell from value <row> of key
 * <section><array name>\<column>\; false at the first it cannot read.
 */
static bool read_registry_array(blk_t *b, const char *section, nmo_object_id_t array,
                                const char *array_name)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t columns = nmo_engine_array_column_count(engine, array);
    int32_t rows = nmo_engine_array_row_count(engine, array);
    bool ok = true;
    for (int32_t c = 0; c < columns && ok; c++) {
        int length = snprintf(NULL, 0, "%s\\%s\\%d\\", section, array_name, (int)c);
        char *column_section = length > 0 ? (char *)malloc((size_t)length + 1u) : NULL;
        if (column_section == NULL) {
            return false;
        }
        snprintf(column_section, (size_t)length + 1u, "%s\\%s\\%d\\", section, array_name, (int)c);
        int32_t type = nmo_engine_array_column_type(engine, array, c);
        for (int32_t r = 0; r < rows && ok; r++) {
            char *entry = row_entry(r);
            nmo_engine_cell_t cell = {.type = type};
            int32_t value = 0;
            float number = 0.0f;
            const char *text = NULL;
            switch (type) {
            case CKARRAYTYPE_INT:
            case CKARRAYTYPE_PARAMETER: /* a Boolean parameter: TT_ReadRegistry refuses others */
                ok = entry != NULL && eng_registry_int(engine, column_section, entry, &value);
                cell.value = (uint32_t)value;
                break;
            case CKARRAYTYPE_FLOAT:
                ok = entry != NULL && eng_registry_float(engine, column_section, entry, &number);
                memcpy(&cell.value, &number, sizeof(number));
                break;
            case CKARRAYTYPE_STRING:
                text = entry != NULL ? eng_registry_value(engine, column_section, entry) : NULL;
                ok = text != NULL;
                cell.text = text;
                break;
            default:
                free(entry);
                continue;
            }
            if (ok) {
                nmo_engine_array_set_cell(engine, array, r, c, &cell);
            }
            free(entry);
        }
        free(column_section);
    }
    return ok;
}

/*
 * TT Exit to System: asks the player to quit (nmo_engine_exit_requested());
 * its input stays active, as in the original.
 */
static int bb_exit_to_system(blk_t *b)
{
    b->engine->host.exit_requested = true;
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT Player Active: Active outside interface mode (In stays active, as in the original). */
static int bb_player_active(blk_t *b)
{
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* The array an input names, emptied of rows and columns; 0 when it names none. */
static nmo_object_id_t emptied_array(blk_t *b, int pos)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_object_id_t array = nmo_engine_block_get_input_object(b, pos);
    if (array == 0 || eng_find_array(engine, array) == NULL) {
        return 0;
    }
    nmo_engine_array_clear(engine, array);
    while (nmo_engine_array_column_count(engine, array) > 0) {
        nmo_engine_array_remove_column(engine, array, 0);
    }
    return array;
}

static void set_int_cell(nmo_engine_t *engine, nmo_object_id_t array, int32_t row, int32_t column,
                         int32_t value)
{
    nmo_engine_cell_t cell = {.type = CKARRAYTYPE_INT, .value = (uint32_t)value};
    nmo_engine_array_set_cell(engine, array, row, column, &cell);
}

static void set_text_cell(nmo_engine_t *engine, nmo_object_id_t array, int32_t row,
                          int32_t column, const char *text)
{
    nmo_engine_cell_t cell = {.type = CKARRAYTYPE_STRING, .text = text};
    nmo_engine_array_set_cell(engine, array, row, column, &cell);
}

/* TT List Driver: DriverName, DriverID, DriverDesc of each render driver; the driver in use. */
static int bb_list_driver(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_object_id_t drivers = emptied_array(b, 0);
    if (drivers == 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_array_insert_column(engine, drivers, -1, CKARRAYTYPE_STRING, NMO_GUID(0u, 0u));
    nmo_engine_array_insert_column(engine, drivers, 0, CKARRAYTYPE_STRING, NMO_GUID(0u, 0u));
    nmo_engine_array_insert_column(engine, drivers, 1, CKARRAYTYPE_INT, NMO_GUID(0u, 0u));
    nmo_engine_array_insert_row(engine, drivers, -1, NULL);
    set_text_cell(engine, drivers, 0, 0, "libnmo");
    set_int_cell(engine, drivers, 0, 1, 0);
    set_text_cell(engine, drivers, 0, 2, "No rendering");
    const int32_t driver = 0;
    set_out(b, 0, &driver, sizeof(driver));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT List ScreenModes: Mode, Width, Height, Bpp of the driver's modes; the mode in use. */
static int bb_list_screen_modes(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t driver = get_in_int(b, 0, 0);
    nmo_object_id_t modes = emptied_array(b, 1);
    if (modes == 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_array_insert_column(engine, modes, -1, CKARRAYTYPE_INT, NMO_GUID(0u, 0u));
    for (int32_t c = 0; c < 3; c++) {
        nmo_engine_array_insert_column(engine, modes, c, CKARRAYTYPE_INT, NMO_GUID(0u, 0u));
    }
    if (driver != 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    for (int32_t i = 0; i < ENG_SCREEN_MODE_COUNT; i++) {
        nmo_engine_array_insert_row(engine, modes, -1, NULL);
        set_int_cell(engine, modes, i, 0, i);
        set_int_cell(engine, modes, i, 1, eng_screen_modes[i].width);
        set_int_cell(engine, modes, i, 2, eng_screen_modes[i].height);
        set_int_cell(engine, modes, i, 3, eng_screen_modes[i].bpp);
    }
    set_out(b, 0, &engine->render.screen_mode, sizeof(engine->render.screen_mode));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Change ScreenMode: asks the player for a mode of the driver's list (an
 * effect); Error for another driver or mode. In stays active, as in the original.
 */
static int bb_change_screen_mode(blk_t *b)
{
    int32_t driver = get_in_int(b, 0, 0);
    int32_t mode = get_in_int(b, 1, 0);
    if (driver != 0 || mode < 0 || mode >= ENG_SCREEN_MODE_COUNT) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    b->engine->render.screen_mode = mode;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_ReadRegistry: value Reg-Entry of key Reg-Section into Data, as Data's
 * type reads it; in SaveArray-Mode, the destination array's cells. OK, or
 * FAILED when a value is missing (In stays active either way).
 */
static int bb_read_registry(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    bool save_array = false;
    nmo_engine_block_get_local_value(b, 0, &save_array, sizeof(save_array));
    const char *section = input_string(b, 0);
    if (section == NULL) {
        section = "";
    }
    bool ok = false;
    if (save_array) {
        nmo_object_id_t array = nmo_engine_block_get_input_object(b, 2);
        const char *array_name = input_string(b, 3);
        if (array_name == NULL || array_name[0] == '\0') {
            array_name = array != 0 ? nmo_engine_object_name(engine, array) : NULL;
        }
        ok = array != 0 && eng_find_array(engine, array) != NULL && array_name != NULL &&
             array_name[0] != '\0' && read_registry_array(b, section, array, array_name);
    } else {
        const char *entry = input_string(b, 1);
        nmo_guid_t type = nmo_engine_block_output_parameter_type(b, 0);
        if (nmo_guid_equals(type, CKPGUID_INT) || nmo_guid_equals(type, CKPGUID_BOOL)) {
            int32_t value = 0;
            ok = eng_registry_int(engine, section, entry, &value);
            if (ok) {
                set_out(b, 0, &value, sizeof(value));
            }
        } else if (nmo_guid_equals(type, CKPGUID_FLOAT)) {
            float value = 0.0f;
            ok = eng_registry_float(engine, section, entry, &value);
            if (ok) {
                set_out(b, 0, &value, sizeof(value));
            }
        } else if (nmo_guid_equals(type, CKPGUID_STRING)) {
            const char *value = eng_registry_value(engine, section, entry);
            ok = value != NULL;
            if (ok) {
                nmo_engine_block_set_output_text(b, 0, value);
            }
        } else {
            ok = true; /* nothing to read */
        }
    }
    OUT_ON(b, ok ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * The game info (CGameInfo)
 * ============================================================================ */

static char *text_copy(const char *text)
{
    return eng_text_copy(text != NULL ? text : "", text != NULL ? strlen(text) : 0);
}

/* sprintf into an exact-size string; NULL when out of memory */
static char *format_text(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = vsnprintf(NULL, 0, format, args);
    va_end(args);
    char *text = length >= 0 ? (char *)malloc((size_t)length + 1u) : NULL;
    if (text != NULL) {
        va_start(args, format);
        vsnprintf(text, (size_t)length + 1u, format, args);
        va_end(args);
    }
    return text;
}

static void free_game_info(eng_game_info_t *info)
{
    if (info != NULL) {
        free(info->game_name);
        free(info->level_name);
        free(info->reg_subkey);
        free(info);
    }
}

static void free_nemo_array(eng_nemo_array_t *nemo)
{
    for (size_t i = 0; i < nemo->elements.count; i++) {
        free(nemo->elements.data[i].text);
        free(nemo->elements.data[i].data);
    }
    free(nemo->elements.data);
    free(nemo->cmo);
    free(nemo->name);
}

void eng_free_tt_interface(nmo_engine_t *engine)
{
    eng_tt_t *tt = eng_tt(engine);
    free_game_info(tt->game_info);
    tt->game_info = NULL;
    free(tt->cmo_name);
    tt->cmo_name = NULL;
    for (size_t i = 0; i < tt->nemo_arrays.count; i++) {
        free_nemo_array(&tt->nemo_arrays.data[i]);
    }
    free(tt->nemo_arrays.data);
    tt->nemo_arrays.data = NULL;
    tt->nemo_arrays.count = tt->nemo_arrays.capacity = 0;
}

/* The game info; without one the block posts TT_MSG_NO_GAMEINFO with its number (the console
   line is not kept) and gets NULL */
static eng_game_info_t *game_info(blk_t *b, int32_t number)
{
    eng_game_info_t *info = eng_tt(b->engine)->game_info;
    if (info == NULL) {
        eng_post_player_message(b->engine, NMO_ENGINE_PLAYER_NO_GAMEINFO, number, 0, NULL);
    }
    return info;
}

enum {
    GI_GAME_SCORE,
    GI_GAME_BONUS,
    GI_GAME_ID,
    GI_LEVEL_SCORE,
    GI_LEVEL_BONUS,
    GI_LEVEL_REACHED,
    GI_LEVEL_ID,
};

static int32_t *game_info_field(eng_game_info_t *info, int field)
{
    switch (field) {
    case GI_GAME_SCORE: return &info->game_score;
    case GI_GAME_BONUS: return &info->game_bonus;
    case GI_GAME_ID: return &info->game_id;
    case GI_LEVEL_SCORE: return &info->level_score;
    case GI_LEVEL_BONUS: return &info->level_bonus;
    case GI_LEVEL_REACHED: return &info->level_reached;
    default: return &info->level_id;
    }
}

/* The TT Get blocks: the fields into the outputs; Out either way */
static int get_fields(blk_t *b, int32_t number, const int *fields, int count)
{
    eng_game_info_t *info = game_info(b, number);
    for (int i = 0; info != NULL && i < count; i++) {
        set_out_int(b, i, *game_info_field(info, fields[i]));
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* The TT Set blocks: the inputs into the fields; Out either way */
static int set_fields(blk_t *b, int32_t number, const int *fields, int count)
{
    eng_game_info_t *info = game_info(b, number);
    for (int i = 0; info != NULL && i < count; i++) {
        *game_info_field(info, fields[i]) = get_in_int(b, i, 0);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

#define GAME_INFO_BLOCK(fn, kind, number, ...)                                                    \
    static int fn(blk_t *b)                                                                       \
    {                                                                                             \
        static const int fields[] = {__VA_ARGS__};                                                \
        return kind(b, number, fields, (int)(sizeof(fields) / sizeof(fields[0])));               \
    }

GAME_INFO_BLOCK(bb_get_game_bonus, get_fields, 0, GI_GAME_BONUS)
GAME_INFO_BLOCK(bb_get_game_data, get_fields, 1, GI_GAME_SCORE, GI_GAME_BONUS)
GAME_INFO_BLOCK(bb_get_game_id, get_fields, 2, GI_GAME_ID)
GAME_INFO_BLOCK(bb_get_game_score, get_fields, 4, GI_GAME_SCORE)
GAME_INFO_BLOCK(bb_set_game_bonus, set_fields, 5, GI_GAME_BONUS)
GAME_INFO_BLOCK(bb_set_game_data, set_fields, 6, GI_GAME_SCORE, GI_GAME_BONUS)
GAME_INFO_BLOCK(bb_set_game_id, set_fields, 7, GI_GAME_ID)
GAME_INFO_BLOCK(bb_set_game_score, set_fields, 9, GI_GAME_SCORE)
GAME_INFO_BLOCK(bb_get_level_bonus, get_fields, 10, GI_LEVEL_BONUS)
GAME_INFO_BLOCK(bb_get_level_data, get_fields, 11, GI_LEVEL_SCORE, GI_LEVEL_BONUS, GI_LEVEL_REACHED)
GAME_INFO_BLOCK(bb_get_level_id, get_fields, 12, GI_LEVEL_ID)
GAME_INFO_BLOCK(bb_get_level_score, get_fields, 14, GI_LEVEL_SCORE)
GAME_INFO_BLOCK(bb_get_level_reached, get_fields, 15, GI_LEVEL_REACHED)
GAME_INFO_BLOCK(bb_set_level_bonus, set_fields, 16, GI_LEVEL_BONUS)
GAME_INFO_BLOCK(bb_set_level_data, set_fields, 17, GI_LEVEL_SCORE, GI_LEVEL_BONUS, GI_LEVEL_REACHED)
/* the DLL's Set Level ID writes the level bonus, not the level ID */
GAME_INFO_BLOCK(bb_set_level_id, set_fields, 18, GI_LEVEL_BONUS)
GAME_INFO_BLOCK(bb_set_level_score, set_fields, 20, GI_LEVEL_SCORE)
GAME_INFO_BLOCK(bb_set_level_reached, set_fields, 21, GI_LEVEL_REACHED)

static int get_name(blk_t *b, int32_t number, bool level)
{
    eng_game_info_t *info = game_info(b, number);
    if (info != NULL) {
        const char *name = level ? info->level_name : info->game_name;
        nmo_engine_block_set_output_text(b, 0, name != NULL ? name : "");
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int set_name(blk_t *b, int32_t number, bool level)
{
    eng_game_info_t *info = game_info(b, number);
    char *name = info != NULL ? text_copy(input_string(b, 0)) : NULL;
    if (name != NULL) {
        char **field = level ? &info->level_name : &info->game_name;
        free(*field);
        *field = name;
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_get_game_name(blk_t *b) { return get_name(b, 3, false); }
static int bb_set_game_name(blk_t *b) { return set_name(b, 8, false); }
static int bb_get_level_name(blk_t *b) { return get_name(b, 13, true); }
static int bb_set_level_name(blk_t *b) { return set_name(b, 19, true); }

/*
 * TT Create new GameInfo: a game info whose registry key is Registry Path,
 * and its address; when there is one already, Out without the address.
 */
static int bb_create_game_info(blk_t *b)
{
    eng_tt_t *tt = eng_tt(b->engine);
    if (tt->game_info == NULL) {
        eng_game_info_t *info = (eng_game_info_t *)calloc(1, sizeof(*info));
        char *key = info != NULL ? text_copy(input_string(b, 0)) : NULL;
        if (key == NULL) {
            free(info);
            return NMO_ENGINE_BR_OK;
        }
        info->reg_subkey = key;
        info->handle = ++tt->game_infos_made;
        tt->game_info = info;
        set_out_int(b, 0, info->handle);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* The address of the game info; 0 (NULL) for none */
static int32_t game_info_handle(blk_t *b)
{
    const eng_game_info_t *info = eng_tt(b->engine)->game_info;
    return info != NULL ? info->handle : 0;
}

/* TT Exists GameInfo ?: True when the address is the game info's (0 without one). */
static int bb_exists_game_info(blk_t *b)
{
    OUT_ON(b, get_in_int(b, 0, 0) == game_info_handle(b) ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* TT Delete GameInfo: Deleted when the address is the game info's (0 without one). */
static int bb_delete_game_info(blk_t *b)
{
    eng_tt_t *tt = eng_tt(b->engine);
    if (get_in_int(b, 0, 0) != game_info_handle(b)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    free_game_info(tt->game_info);
    tt->game_info = NULL;
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * The player
 * ============================================================================ */

/* TT Load CMO: posts TT_MSG_CMO_LOAD with the name; nothing without a game info. */
static int bb_load_cmo(blk_t *b)
{
    eng_tt_t *tt = eng_tt(b->engine);
    if (tt->game_info == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    char *name = text_copy(input_string(b, 0));
    if (name == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    free(tt->cmo_name);
    tt->cmo_name = name;
    eng_post_player_message(b->engine, NMO_ENGINE_PLAYER_CMO_LOAD, 0, 0, name);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT Restart CMO: posts TT_MSG_CMO_RESTART; nothing without a game info. */
static int bb_restart_cmo(blk_t *b)
{
    if (eng_tt(b->engine)->game_info != NULL) {
        eng_post_player_message(b->engine, NMO_ENGINE_PLAYER_CMO_RESTART, 0, 0, NULL);
        OUT_ON(b, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* TT Limit Framerate: posts TT_MSG_LIMIT_FPS with the frame rate (60 without one). */
static int bb_limit_framerate(blk_t *b)
{
    eng_post_player_message(b->engine, NMO_ENGINE_PLAYER_LIMIT_FPS, get_in_int(b, 0, 60), 0, NULL);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT Window Activate?: Activate once after the player's window was activated, else else. */
static int bb_window_activate(blk_t *b)
{
    bool activated = b->engine->host.window_activated;
    b->engine->host.window_activated = false;
    OUT_ON(b, activated ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* TT Set TaskSwitchHandler: the task switch flag (on without a value); nothing without a game
   info. */
static int bb_set_task_switch_handler(blk_t *b)
{
    eng_tt_t *tt = eng_tt(b->engine);
    if (tt->game_info != NULL) {
        tt->task_switch = get_in_int(b, 0, 1) != 0;
        OUT_ON(b, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* TT Set Rookie: a rookie when Is Rookie is exactly 1 (TRUE). */
static int bb_set_rookie(blk_t *b)
{
    OUT_ON(b, 0);
    eng_tt(b->engine)->rookie = get_in_int(b, 0, 0) == 1;
    return NMO_ENGINE_BR_OK;
}

static int bb_is_rookie(blk_t *b)
{
    OUT_ON(b, eng_tt(b->engine)->rookie ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * The installed arrays (CNemoArrayList)
 * ============================================================================ */

/* CNemoArrayList::Find: the array kept for that CMO and the array's name; NULL for none */
static eng_nemo_array_t *nemo_find(nmo_engine_t *engine, const char *cmo, nmo_object_id_t array)
{
    eng_tt_t *tt = eng_tt(engine);
    if (array == 0 || eng_find_array(engine, array) == NULL) {
        return NULL;
    }
    const char *name = nmo_engine_object_name(engine, array);
    for (size_t i = 0; i < tt->nemo_arrays.count; i++) {
        eng_nemo_array_t *nemo = &tt->nemo_arrays.data[i];
        if (strcmp(nemo->cmo, cmo != NULL ? cmo : "") == 0 &&
            strcmp(nemo->name, name != NULL ? name : "") == 0) {
            return nemo;
        }
    }
    return NULL;
}

/* An element takes a cell's value (CNemoElement::SetValue / SetString) */
static bool nemo_set(eng_nemo_element_t *element, const nmo_engine_cell_t *cell)
{
    char *text = NULL;
    void *data = NULL;
    if (cell->type == CKARRAYTYPE_STRING) {
        text = text_copy(cell->text);
        if (text == NULL) {
            return false;
        }
    } else if (cell->type == CKARRAYTYPE_PARAMETER && cell->size > 0) {
        data = malloc(cell->size);
        if (data == NULL) {
            return false;
        }
        memcpy(data, cell->data, cell->size);
    }
    free(element->text);
    free(element->data);
    *element = (eng_nemo_element_t){cell->type, cell->value, text, data,
                                    data != NULL ? cell->size : 0, cell->parameter_type};
    return true;
}

/* CNemoArray's constructor: the array's first row, one element a column (none without rows) */
static void nemo_add(nmo_engine_t *engine, const char *cmo, nmo_object_id_t array)
{
    eng_tt_t *tt = eng_tt(engine);
    const char *name = nmo_engine_object_name(engine, array);
    eng_nemo_array_t nemo = {text_copy(cmo), text_copy(name), {0}};
    bool ok = nemo.cmo != NULL && nemo.name != NULL;
    int32_t columns = nmo_engine_array_row_count(engine, array) > 0
                          ? nmo_engine_array_column_count(engine, array) : 0;
    for (int32_t c = 0; ok && c < columns; c++) {
        nmo_engine_cell_t cell = {0};
        eng_nemo_element_t *element = NULL;
        ok = nmo_engine_array_get_cell(engine, array, 0, c, &cell) == NMO_OK &&
             ENG_VEC_PUSH(nemo.elements, element);
        if (ok) {
            *element = (eng_nemo_element_t){0};
            ok = nemo_set(element, &cell);
        }
    }
    eng_nemo_array_t *slot = NULL;
    if (!ok || !ENG_VEC_PUSH(tt->nemo_arrays, slot)) {
        free_nemo_array(&nemo);
        return;
    }
    *slot = nemo;
}

/* CNemoArray::Read: the kept values into the first row, column by column while the types agree,
   up to a column without a type; only when the array has a row and as many columns */
static void nemo_read(nmo_engine_t *engine, const eng_nemo_array_t *nemo, nmo_object_id_t array)
{
    if (nmo_engine_array_row_count(engine, array) < 1 ||
        nmo_engine_array_column_count(engine, array) != (int32_t)nemo->elements.count) {
        return;
    }
    for (int32_t c = 0; c < (int32_t)nemo->elements.count; c++) {
        int32_t type = nmo_engine_array_column_type(engine, array, c);
        if (type == 0) {
            break;
        }
        const eng_nemo_element_t *element = &nemo->elements.data[c];
        if (element->type == type) {
            nmo_engine_cell_t cell = {type, element->value, element->text, element->data,
                                      element->size, element->parameter_type};
            nmo_engine_array_set_cell(engine, array, 0, c, &cell);
        }
    }
}

/* CNemoArray::Write: the first row into the kept values, likewise; as in the DLL, a type that
   does not agree keeps the column, so the next value is compared with it again */
static void nemo_write(nmo_engine_t *engine, eng_nemo_array_t *nemo, nmo_object_id_t array)
{
    if (nmo_engine_array_row_count(engine, array) < 1 ||
        nmo_engine_array_column_count(engine, array) != (int32_t)nemo->elements.count) {
        return;
    }
    int32_t column = 0;
    for (size_t i = 0; i < nemo->elements.count; i++) {
        eng_nemo_element_t *element = &nemo->elements.data[i];
        if (element->type != nmo_engine_array_column_type(engine, array, column)) {
            continue;
        }
        nmo_engine_cell_t cell = {0};
        if (nmo_engine_array_get_cell(engine, array, 0, column, &cell) == NMO_OK) {
            nemo_set(element, &cell);
        }
        column++;
    }
}

/*
 * TT Install Array / TT Uninstall Array: keep the array's first row for the
 * CMO when it is not kept yet / forget it. The "show message" box is not
 * shown. In goes off.
 */
static int bb_install_array(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_object_id_t array = nmo_engine_block_get_input_object(b, 0);
    const char *cmo = input_string(b, 1);
    if (array != 0 && eng_find_array(b->engine, array) != NULL &&
        nemo_find(b->engine, cmo, array) == NULL) {
        nemo_add(b->engine, cmo, array);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_uninstall_array(blk_t *b)
{
    IN_OFF(b, 0);
    eng_tt_t *tt = eng_tt(b->engine);
    eng_nemo_array_t *nemo = nemo_find(b->engine, input_string(b, 1),
                                       nmo_engine_block_get_input_object(b, 0));
    if (nemo != NULL) {
        size_t index = (size_t)(nemo - tt->nemo_arrays.data);
        free_nemo_array(nemo);
        memmove(nemo, nemo + 1, (tt->nemo_arrays.count - index - 1) * sizeof(*nemo));
        tt->nemo_arrays.count--;
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT Read Array / TT Write Array: the kept row into the array's first row / back. */
static int bb_read_array(blk_t *b)
{
    nmo_object_id_t array = nmo_engine_block_get_input_object(b, 0);
    eng_nemo_array_t *nemo = nemo_find(b->engine, input_string(b, 1), array);
    if (nemo != NULL) {
        nemo_read(b->engine, nemo, array);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_write_array(blk_t *b)
{
    nmo_object_id_t array = nmo_engine_block_get_input_object(b, 0);
    eng_nemo_array_t *nemo = nemo_find(b->engine, input_string(b, 1), array);
    if (nemo != NULL) {
        nemo_write(b->engine, nemo, array);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * The registry
 * ============================================================================ */

/* The engine's registry holds text: a value is written as the readers parse it back */
static void registry_write_int(nmo_engine_t *engine, const char *key, const char *name, int32_t value)
{
    char *text = format_text("%d", (int)value);
    if (text != NULL) {
        nmo_engine_set_registry_value(engine, key, name != NULL ? name : "", text);
    }
    free(text);
}

static void registry_write_float(nmo_engine_t *engine, const char *key, const char *name, float value)
{
    char *text = format_text("%.9g", (double)value);
    if (text != NULL) {
        nmo_engine_set_registry_value(engine, key, name != NULL ? name : "", text);
    }
    free(text);
}

static void registry_write_text(nmo_engine_t *engine, const char *key, const char *name,
                                const char *value)
{
    nmo_engine_set_registry_value(engine, key, name != NULL ? name : "", value != NULL ? value : "");
}

/* The game info's key followed by the RegKey input ("%s%s"); NULL without a game info */
static char *game_key(blk_t *b, int pos)
{
    const eng_game_info_t *info = eng_tt(b->engine)->game_info;
    const char *key = input_string(b, pos);
    return info != NULL ? format_text("%s%s", info->reg_subkey, key != NULL ? key : "") : NULL;
}

enum { REG_BOOL, REG_INT, REG_FLOAT, REG_STRING };

/*
 * TT Get <type> Value From Registry: value Value Name of the game info's
 * key + RegKey. OK; FAILED when the key or the value is missing (a missing
 * key outputs FALSE, 1, 0 or "", a missing integer 2). Nothing without a game
 * info.
 */
static int registry_get(blk_t *b, int kind)
{
    nmo_engine_t *engine = b->engine;
    char *key = game_key(b, 0);
    if (key == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    const char *name = input_string(b, 1);
    if (name == NULL) {
        name = "";
    }
    bool ok = false;
    if (!eng_registry_has_key(engine, key)) {
        if (kind == REG_STRING) {
            nmo_engine_block_set_output_text(b, 0, "");
        } else {
            set_out_int(b, 0, kind == REG_INT ? 1 : 0);
        }
    } else if (kind == REG_STRING) {
        const char *value = eng_registry_value(engine, key, name);
        ok = value != NULL && strlen(value) < 256u; /* RegQueryValueEx into 256 bytes */
        if (ok) {
            nmo_engine_block_set_output_text(b, 0, value);
        }
    } else if (kind == REG_FLOAT) {
        float value = 0.0f;
        ok = eng_registry_float(engine, key, name, &value);
        if (ok) {
            set_out_float(b, 0, value);
        }
    } else {
        int32_t value = 0;
        ok = eng_registry_int(engine, key, name, &value);
        if (ok || kind == REG_INT) {
            set_out_int(b, 0, ok ? value : 2);
        }
    }
    free(key);
    OUT_ON(b, ok ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Set <type> Value To Registry: the value into Value Name of the game
 * info's key + RegKey (OK). Without a game info nothing, but FAILED for the
 * integer.
 */
static int registry_set(blk_t *b, int kind)
{
    nmo_engine_t *engine = b->engine;
    char *key = game_key(b, 1);
    if (key == NULL) {
        if (kind == REG_INT) {
            OUT_ON(b, 1);
        }
        return NMO_ENGINE_BR_OK;
    }
    const char *name = input_string(b, 2);
    if (kind == REG_STRING) {
        registry_write_text(engine, key, name, input_string(b, 0));
    } else if (kind == REG_FLOAT) {
        registry_write_float(engine, key, name, get_in_float(b, 0, 0.0f));
    } else {
        registry_write_int(engine, key, name, get_in_int(b, 0, 0));
    }
    free(key);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_get_bool_registry(blk_t *b) { return registry_get(b, REG_BOOL); }
static int bb_get_int_registry(blk_t *b) { return registry_get(b, REG_INT); }
static int bb_get_float_registry(blk_t *b) { return registry_get(b, REG_FLOAT); }
static int bb_get_string_registry(blk_t *b) { return registry_get(b, REG_STRING); }
static int bb_set_bool_registry(blk_t *b) { return registry_set(b, REG_BOOL); }
static int bb_set_int_registry(blk_t *b) { return registry_set(b, REG_INT); }
static int bb_set_float_registry(blk_t *b) { return registry_set(b, REG_FLOAT); }
static int bb_set_string_registry(blk_t *b) { return registry_set(b, REG_STRING); }

/*
 * SaveArray-Mode of TT_WriteRegistry: each cell as value <row> of key
 * <section>\<array name>\<column>\ (TT_ReadRegistry's layout); integer,
 * float, string and Boolean parameter columns, others skipped. False at a
 * parameter column of another type.
 */
static bool write_registry_array(nmo_engine_t *engine, const char *section, nmo_object_id_t array)
{
    const char *array_name = nmo_engine_object_name(engine, array);
    int32_t columns = nmo_engine_array_column_count(engine, array);
    int32_t rows = nmo_engine_array_row_count(engine, array);
    for (int32_t c = 0; c < columns; c++) {
        int32_t type = nmo_engine_array_column_type(engine, array, c);
        char *key = format_text("%s\\%s\\%d\\", section, array_name != NULL ? array_name : "", (int)c);
        for (int32_t r = 0; key != NULL && r < rows; r++) {
            nmo_engine_cell_t cell = {0};
            if (nmo_engine_array_get_cell(engine, array, r, c, &cell) != NMO_OK) {
                continue;
            }
            char *entry = format_text("%d", (int)r);
            float number = 0.0f;
            uint8_t flag = 0;
            switch (type) {
            case CKARRAYTYPE_INT:
                registry_write_int(engine, key, entry, (int32_t)cell.value);
                break;
            case CKARRAYTYPE_FLOAT:
                memcpy(&number, &cell.value, sizeof(number));
                registry_write_float(engine, key, entry, number);
                break;
            case CKARRAYTYPE_PARAMETER:
                if (!nmo_guid_equals(cell.parameter_type, CKPGUID_BOOL)) {
                    free(entry);
                    free(key);
                    return false;
                }
                if (cell.data != NULL && cell.size > 0) {
                    memcpy(&flag, cell.data, 1); /* the DLL writes the value's low byte */
                }
                registry_write_int(engine, key, entry, flag);
                break;
            case CKARRAYTYPE_STRING:
                registry_write_text(engine, key, entry, cell.text);
                break;
            default:
                break;
            }
            free(entry);
        }
        free(key);
    }
    return true;
}

/*
 * TT_WriteRegistry: Data into value Reg-Entry of key Reg-Section (under
 * HKEY_CURRENT_USER, without the game info's key), as Data's type writes it:
 * integer, float, Boolean (its low byte) or string, nothing for another
 * type; in SaveArray-Mode, the destination array's cells. OK; FAILED at a
 * parameter column that is not a Boolean, or without an array (the DLL
 * crashes there).
 */
static int bb_write_registry(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    bool save_array = false;
    nmo_engine_block_get_local_value(b, 0, &save_array, sizeof(save_array));
    const char *section = input_string(b, 0);
    if (section == NULL) {
        section = "";
    }
    const char *entry = input_string(b, 1);
    bool ok = true;
    if (save_array) {
        nmo_object_id_t array = nmo_engine_block_get_input_object(b, 2);
        ok = array != 0 && eng_find_array(engine, array) != NULL &&
             write_registry_array(engine, section, array);
    } else {
        nmo_guid_t type = nmo_engine_block_input_parameter_type(b, 3);
        if (nmo_guid_equals(type, CKPGUID_INT)) {
            registry_write_int(engine, section, entry, get_in_int(b, 3, 0));
        } else if (nmo_guid_equals(type, CKPGUID_FLOAT)) {
            registry_write_float(engine, section, entry, get_in_float(b, 3, 0.0f));
        } else if (nmo_guid_equals(type, CKPGUID_BOOL)) {
            registry_write_int(engine, section, entry, (uint8_t)get_in_int(b, 3, 0));
        } else if (nmo_guid_equals(type, CKPGUID_STRING)) {
            registry_write_text(engine, section, entry, input_string(b, 3));
        }
    }
    OUT_ON(b, ok ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_tt_interface_manager_blocks[] = {
    {NMO_GUID_INIT(0x5bc50af4, 0x41e92536), "TT Exit to System", bb_exit_to_system, NULL, NULL},
    {NMO_GUID_INIT(0x0694658d, 0x7ffd236f), "TT Player Active", bb_player_active, NULL, NULL},
    {NMO_GUID_INIT(0x62d00456, 0x30eb4245), "TT List Driver", bb_list_driver, NULL, NULL},
    {NMO_GUID_INIT(0x4e7a0194, 0x040328fd), "TT List ScreenModes", bb_list_screen_modes, NULL, NULL},
    {NMO_GUID_INIT(0x460044b5, 0x6e927b66), "TT_ReadRegistry", bb_read_registry, NULL, NULL},
    {NMO_GUID_INIT(0x38b84d97, 0x13932f28), "TT Change ScreenMode", bb_change_screen_mode, NULL, NULL},    {NMO_GUID_INIT(0x0b585f6f, 0x46253c21), "TT Restart CMO", bb_restart_cmo, NULL, NULL},
    {NMO_GUID_INIT(0x63dd09a7, 0x567f7218), "TT Load CMO", bb_load_cmo, NULL, NULL},
    {NMO_GUID_INIT(0x41f47ec4, 0x69470bbd), "TT Install Array", bb_install_array, NULL, NULL},
    {NMO_GUID_INIT(0x07414af4, 0x505e52ee), "TT Write Array", bb_write_array, NULL, NULL},
    {NMO_GUID_INIT(0x5c825017, 0x6d367973), "TT Read Array", bb_read_array, NULL, NULL},
    {NMO_GUID_INIT(0x77dd56cf, 0x63dc30b4), "TT Uninstall Array", bb_uninstall_array, NULL, NULL},
    {NMO_GUID_INIT(0x0a8b6c27, 0x402b43b4), "TT Create new GameInfo", bb_create_game_info, NULL, NULL},
    {NMO_GUID_INIT(0x56173aa5, 0x73ff1a7f), "TT Exists GameInfo ?", bb_exists_game_info, NULL, NULL},
    {NMO_GUID_INIT(0x29f17890, 0x2e0117a2), "TT Delete GameInfo ", bb_delete_game_info, NULL, NULL},
    {NMO_GUID_INIT(0x12a40e8f, 0x6f2d0613), "TT Limit Framerate", bb_limit_framerate, NULL, NULL},
    {NMO_GUID_INIT(0x70936783, 0x7fee4a3b), "TT Window Activate?", bb_window_activate, NULL, NULL},
    {NMO_GUID_INIT(0x10043a79, 0x03ba2461), "TT Set TaskSwitchHandler", bb_set_task_switch_handler, NULL, NULL},
    {NMO_GUID_INIT(0x7c805c49, 0x78a601d3), "TT Set Game Data", bb_set_game_data, NULL, NULL},
    {NMO_GUID_INIT(0x2a5a3d0b, 0x04e6466c), "TT Set Game Score", bb_set_game_score, NULL, NULL},
    {NMO_GUID_INIT(0x78587cf6, 0x0ddb1bb0), "TT Set Game Bonus", bb_set_game_bonus, NULL, NULL},
    {NMO_GUID_INIT(0x30641426, 0x12403bb9), "TT Set Game Name", bb_set_game_name, NULL, NULL},
    {NMO_GUID_INIT(0x36af3944, 0x67c75aa7), "TT Set Game ID", bb_set_game_id, NULL, NULL},
    {NMO_GUID_INIT(0x16875322, 0x72422c5c), "TT  Get Game Data", bb_get_game_data, NULL, NULL},
    {NMO_GUID_INIT(0x34951557, 0x4d180895), "TT Get Game Score", bb_get_game_score, NULL, NULL},
    {NMO_GUID_INIT(0x12480cb5, 0x4816349f), "TT Get Game Bonus", bb_get_game_bonus, NULL, NULL},
    {NMO_GUID_INIT(0x39b21b0c, 0x5db65810), "TT Get Game Name", bb_get_game_name, NULL, NULL},
    {NMO_GUID_INIT(0x63527b75, 0x1f9a40d9), "TT Get Game ID", bb_get_game_id, NULL, NULL},
    {NMO_GUID_INIT(0x02030ea1, 0x30182971), "TT Set Level Data", bb_set_level_data, NULL, NULL},
    {NMO_GUID_INIT(0x568e2c3c, 0x24547ef7), "TT Set Level Score", bb_set_level_score, NULL, NULL},
    {NMO_GUID_INIT(0x0cf151b2, 0x0b1b6001), "TT Set Level Reached", bb_set_level_reached, NULL, NULL},
    {NMO_GUID_INIT(0x4f9b6fa1, 0x57040b1b), "TT Set Level Bonus", bb_set_level_bonus, NULL, NULL},
    {NMO_GUID_INIT(0x7aec3d66, 0x4aa904c5), "TT Set Level Name", bb_set_level_name, NULL, NULL},
    {NMO_GUID_INIT(0x20b86a4c, 0x3c62472a), "TT Set Level ID", bb_set_level_id, NULL, NULL},
    {NMO_GUID_INIT(0x6bca40f9, 0x42641a24), "TT Get Level Data", bb_get_level_data, NULL, NULL},
    {NMO_GUID_INIT(0x7fe65a65, 0x490a49b5), "TT Get Level Score", bb_get_level_score, NULL, NULL},
    {NMO_GUID_INIT(0x2e8344e0, 0x05101432), "TT Get Level Reached", bb_get_level_reached, NULL, NULL},
    {NMO_GUID_INIT(0x573705fa, 0x1710468e), "TT Get Level Bonus", bb_get_level_bonus, NULL, NULL},
    {NMO_GUID_INIT(0x6a3d4643, 0x4f0462dd), "TT Get Level Name", bb_get_level_name, NULL, NULL},
    {NMO_GUID_INIT(0x649011fe, 0x437d5918), "TT Get Level ID", bb_get_level_id, NULL, NULL},
    {NMO_GUID_INIT(0x74a35f75, 0x3ad00faa), "TT Set String Value To Registry", bb_set_string_registry, NULL, NULL},
    {NMO_GUID_INIT(0x175c07d1, 0x1d235544), "TT Set Integer Value To Registry", bb_set_int_registry, NULL, NULL},
    {NMO_GUID_INIT(0x0060522f, 0x041d4c1f), "TT Set Float Value To Registry", bb_set_float_registry, NULL, NULL},
    {NMO_GUID_INIT(0x69fb3783, 0x50262517), "TT Set Boolean Value To Registry", bb_set_bool_registry, NULL, NULL},
    {NMO_GUID_INIT(0x75ff55a7, 0x378205d0), "TT Get String Value From Registry", bb_get_string_registry, NULL, NULL},
    {NMO_GUID_INIT(0x50c02b30, 0x2e5a2eda), "TT Get Integer Value From Registry", bb_get_int_registry, NULL, NULL},
    {NMO_GUID_INIT(0x00fe6fe4, 0x12a66150), "TT Get Float Value From Registry", bb_get_float_registry, NULL, NULL},
    {NMO_GUID_INIT(0x5ace7e01, 0x199b76fd), "TT Get Boolean Value From Registry", bb_get_bool_registry, NULL, NULL},
    {NMO_GUID_INIT(0x7aa8084e, 0x1ee229b7), "TT_WriteRegistry", bb_write_registry, NULL, NULL},
    {NMO_GUID_INIT(0x77de64df, 0x694f141f), "TT Set Rookie", bb_set_rookie, NULL, NULL},
    {NMO_GUID_INIT(0x6dc06abb, 0x44451de5), "TT Is Rookie", bb_is_rookie, NULL, NULL},
};

nmo_status_t eng_register_tt_interface_manager(nmo_engine_t *engine)
{
    nmo_status_t status = eng_tt_add(engine);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_tt_interface_manager_blocks) : status;
}
