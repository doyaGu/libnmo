#ifndef NMO_LUA_FOLD_MAP_PARSER_H
#define NMO_LUA_FOLD_MAP_PARSER_H

#include "behavior/nmo_behavior_edit.h"

#include <stdbool.h>
#include <stddef.h>

#include "lua.h"

bool nmo_lua_fold_map_parse(lua_State *state,
                            int options_index,
                            const char *field_name,
                            nmo_behavior_fold_map_kind_t kind,
                            nmo_behavior_fold_map_t **out_maps,
                            size_t *out_count,
                            const char **out_error);

#endif /* NMO_LUA_FOLD_MAP_PARSER_H */
