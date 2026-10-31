/**
 * @file interface_manager.c
 * @brief Ports of the TT Interface Manager building blocks.
 *
 * Each function does what the block's function in TT_InterfaceManager_RT.dll
 * does (the reference is the Ballanced source of the blocks, or the DLL where
 * they differ). The machine is a fixed one: one render driver with 4:3 screen
 * modes in 16 and 32 bits, not in interface mode. The registry holds what the
 * host set.
 */

#include "../../engine_block_util.h"
#include "../../engine_tt.h"

#include "object/nmo_object_enum_defs.h"
#include "type/nmo_type_guids.h"

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

static const nmo_engine_block_desc_t eng_tt_interface_manager_blocks[] = {
    {NMO_GUID(0x5bc50af4, 0x41e92536), "TT Exit to System", bb_exit_to_system, NULL, NULL},
    {NMO_GUID(0x0694658d, 0x7ffd236f), "TT Player Active", bb_player_active, NULL, NULL},
    {NMO_GUID(0x62d00456, 0x30eb4245), "TT List Driver", bb_list_driver, NULL, NULL},
    {NMO_GUID(0x4e7a0194, 0x040328fd), "TT List ScreenModes", bb_list_screen_modes, NULL, NULL},
    {NMO_GUID(0x460044b5, 0x6e927b66), "TT_ReadRegistry", bb_read_registry, NULL, NULL},
    {NMO_GUID(0x38b84d97, 0x13932f28), "TT Change ScreenMode", bb_change_screen_mode, NULL, NULL},
};

nmo_status_t eng_register_tt_interface_manager(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_tt_interface_manager_blocks);
}
