# Layering audit
#
# The library is organised as a stack (see README, "Layer Stack"). A source
# file in one layer may include public headers from layers at or below its
# own. Every upward include that exists today is listed in
# tests/layering_allowlist.txt so the debt is visible and can be paid down;
# this audit fails only when a new upward include is introduced.
#
# Usage: cmake -DSOURCE_DIR=<repo root> -P layering_audit.cmake

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

# Lowest layer first. Must match the README layer table.
set(layers core io format type extension object session runtime document chunk behavior export lua project)

file(STRINGS "${SOURCE_DIR}/tests/layering_allowlist.txt" allowlist_lines)
set(allowlist "")
foreach(line IN LISTS allowlist_lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()
    list(APPEND allowlist "${line}")
endforeach()

file(GLOB_RECURSE source_files "${SOURCE_DIR}/src/*.c" "${SOURCE_DIR}/src/*.h")

set(violations "")
set(seen "")
foreach(source_file IN LISTS source_files)
    file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${source_file}")
    if(NOT relative_path MATCHES "^src/([a-z]+)/")
        continue()
    endif()
    set(file_layer "${CMAKE_MATCH_1}")
    list(FIND layers "${file_layer}" file_rank)
    if(file_rank EQUAL -1)
        continue()
    endif()

    file(STRINGS "${source_file}" include_lines REGEX "^[ \t]*#[ \t]*include[ \t]*\"[a-z]+/")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "\"([a-z]+)/([^\"]+)\"")
            continue()
        endif()
        set(include_layer "${CMAKE_MATCH_1}")
        set(include_path "${CMAKE_MATCH_1}/${CMAKE_MATCH_2}")
        list(FIND layers "${include_layer}" include_rank)
        if(include_rank EQUAL -1 OR NOT include_rank GREATER file_rank)
            continue()
        endif()
        set(entry "${relative_path} ${include_path}")
        list(APPEND seen "${entry}")
        list(FIND allowlist "${entry}" allowed)
        if(allowed EQUAL -1)
            string(APPEND violations
                "${relative_path} (${file_layer}) includes ${include_path} (${include_layer}, higher layer)\n")
        endif()
    endforeach()
endforeach()

# Report allowlist entries that no longer exist so the list stays honest.
set(stale "")
foreach(entry IN LISTS allowlist)
    list(FIND seen "${entry}" present)
    if(present EQUAL -1)
        string(APPEND stale "  ${entry}\n")
    endif()
endforeach()

if(violations)
    message(FATAL_ERROR
        "Layering audit failed: new upward include(s)\n${violations}"
        "Move the dependency down the stack, or add the line to tests/layering_allowlist.txt with a justification commit.\n")
endif()
if(stale)
    message(FATAL_ERROR
        "Layering audit: allowlist entries no longer needed, remove them from tests/layering_allowlist.txt:\n${stale}")
endif()

list(LENGTH allowlist allowlist_count)
message(STATUS "Layering audit passed (${allowlist_count} allowlisted upward includes)")
