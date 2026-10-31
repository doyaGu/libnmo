/**
 * @file engine_tt.h
 * @brief The Terratools managers Ballance's blocks use: the time manager,
 *        the database manager and the debug manager.
 */

#ifndef NMO_ENGINE_TT_H
#define NMO_ENGINE_TT_H

#include "engine_base.h"

typedef struct eng_tt {
    float time;   /* TT Time Manager: TT_Timer's time, and whether it runs */
    bool timing;
    char *database_file; /* TT Database Manager: TT Set Database Properties */
    bool database_crypted;
    ENG_VEC(char *) database_arrays; /* the arrays it saves (DatabaseManager::Register) */
    bool debug_mode; /* CTTDebugManager::m_DebugMode, which TT_DebugON sets */
} eng_tt_t;

/* blocks/tt/database.c */
void eng_free_tt(nmo_engine_t *engine);

#endif /* NMO_ENGINE_TT_H */
