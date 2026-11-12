# Layering audit
#
# The library is organised as a stack (see README, "Layer Stack"). A source
# file in one layer may include public headers from layers at or below its
# own. Every upward include that exists today is listed in
# tests/layering_allowlist.txt so the debt is visible and can be paid down;
# this audit fails only when a new upward include is introduced.
# tests/layering_calls_audit.py checks the functions a file uses, which an
# include check cannot see when a header pulls in another layer transitively.
#
# Usage: cmake -DSOURCE_DIR=<repo root> -P layering_audit.cmake

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

# Lowest layer first. Must match the README layer table.
set(layers platform core io format type extension object export session runtime document chunk behavior engine bb render view edit lua project)

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

    # A relative include of another layer's private header ("../runtime/x.h")
    # is a dependency on that layer too.
    file(STRINGS "${source_file}" include_lines
        REGEX "^[ \t]*#[ \t]*include[ \t]*\"(\\.\\./)?[a-z]+/")
    foreach(include_line IN LISTS include_lines)
        if(NOT include_line MATCHES "\"(\\.\\./)?([a-z]+)/([^\"]+)\"")
            continue()
        endif()
        set(include_prefix "${CMAKE_MATCH_1}")
        set(CMAKE_MATCH_1 "${CMAKE_MATCH_2}")
        set(CMAKE_MATCH_2 "${CMAKE_MATCH_3}")
        set(include_layer "${CMAKE_MATCH_1}")
        set(include_path "${include_prefix}${CMAKE_MATCH_1}/${CMAKE_MATCH_2}")
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

# The core's public headers must not pull in the optional components: engine/,
# bb/, render/, edit/, lua/ and project/ headers (and Lua's own lua.h) belong to
# nmo_engine, the block packs (nmo_bb_*), nmo_render, nmo_edit, nmo_lua and
# nmo_project only. The components may include each other's headers downward: engine and
# edit, the packs on top of engine, then lua and project on top of edit.
file(GLOB_RECURSE public_headers "${SOURCE_DIR}/include/*.h")
set(component_violations "")
foreach(header IN LISTS public_headers)
    file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${header}")
    if(relative_path MATCHES "^include/(engine|bb|render|edit|lua|project)/"
       OR relative_path STREQUAL "include/nmo_engine.h"
       OR relative_path STREQUAL "include/nmo_edit.h"
       OR relative_path STREQUAL "include/nmo_lua.h"
       OR relative_path STREQUAL "include/nmo_project.h")
        continue()
    endif()
    file(STRINGS "${header}" include_lines
        REGEX "^[ \t]*#[ \t]*include[ \t]*\"(engine|bb|render|edit|lua|project)/")
    foreach(include_line IN LISTS include_lines)
        string(APPEND component_violations "${relative_path}: ${include_line}\n")
    endforeach()
endforeach()
if(component_violations)
    message(FATAL_ERROR
        "Layering audit failed: core public header includes an optional component\n"
        "${component_violations}"
        "Move the declaration into include/engine, include/bb, include/render, include/edit, include/lua or include/project, or forward-declare the type.\n")
endif()

# The engine knows no block pack: its public headers include no bb/ header
# (the src/ check above covers its sources). Each pack builds on the engine
# through src/bb/bb_sdk.h and knows no other pack: a file of src/bb/<pack>/
# includes neither another pack's files nor its public header.
set(pack_violations "")
file(GLOB engine_headers "${SOURCE_DIR}/include/engine/*.h" "${SOURCE_DIR}/include/nmo_engine.h")
foreach(header IN LISTS engine_headers)
    file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${header}")
    file(STRINGS "${header}" include_lines REGEX "^[ \t]*#[ \t]*include[ \t]*\"bb/")
    foreach(include_line IN LISTS include_lines)
        string(APPEND pack_violations "${relative_path}: ${include_line}\n")
    endforeach()
endforeach()
file(GLOB pack_dirs LIST_DIRECTORIES true "${SOURCE_DIR}/src/bb/*")
set(packs "")
foreach(pack_dir IN LISTS pack_dirs)
    if(IS_DIRECTORY "${pack_dir}")
        get_filename_component(pack "${pack_dir}" NAME)
        list(APPEND packs "${pack}")
    endif()
endforeach()
foreach(pack IN LISTS packs)
    file(GLOB_RECURSE pack_files "${SOURCE_DIR}/src/bb/${pack}/*.c" "${SOURCE_DIR}/src/bb/${pack}/*.h")
    foreach(pack_file IN LISTS pack_files)
        file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${pack_file}")
        file(STRINGS "${pack_file}" include_lines REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
        foreach(include_line IN LISTS include_lines)
            foreach(other IN LISTS packs)
                if(NOT other STREQUAL pack AND
                   (include_line MATCHES "\"bb/nmo_bb_${other}\\.h\"" OR
                    include_line MATCHES "\"(\\.\\./)+${other}/"))
                    string(APPEND pack_violations "${relative_path} (${pack}): ${include_line}\n")
                endif()
            endforeach()
        endforeach()
    endforeach()
endforeach()
if(pack_violations)
    message(FATAL_ERROR
        "Layering audit failed: the engine includes a block pack, or a pack another pack\n"
        "${pack_violations}"
        "Keep the engine free of packs; share what packs need through the engine (src/engine).\n")
endif()

# Graphics APIs and window systems stay at the edges, so that a backend or a
# host can be replaced alone: a graphics API's headers (OpenGL, OpenGL ES,
# Vulkan, Metal, Direct3D) only in a backend of the renderer,
# src/render/<backend>/, and SDL's only in the view, src/view, and in the
# renderer's SDL_GPU backend, src/render/gpu.
set(platform_violations "")
file(GLOB_RECURSE platform_files "${SOURCE_DIR}/src/*.c" "${SOURCE_DIR}/src/*.h" "${SOURCE_DIR}/src/*.m")
foreach(platform_file IN LISTS platform_files)
    file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${platform_file}")
    file(STRINGS "${platform_file}" include_lines
        REGEX "^[ \t]*#[ \t]*(include|import)[ \t]*<(GL|GLES[0-9]*|OpenGL|OpenGLES|vulkan|Metal|SDL[0-9]*)/|^[ \t]*#[ \t]*include[ \t]*<d3d[0-9a-z_]*\\.h>")
    foreach(include_line IN LISTS include_lines)
        if(include_line MATCHES "<SDL")
            if(NOT relative_path MATCHES "^src/(view|render/gpu)/")
                string(APPEND platform_violations "${relative_path}: ${include_line} (SDL outside src/view and src/render/gpu)\n")
            endif()
        elseif(NOT relative_path MATCHES "^src/render/[a-z0-9_]+/")
            string(APPEND platform_violations "${relative_path}: ${include_line} (a graphics API outside a renderer backend)\n")
        endif()
    endforeach()
endforeach()
if(platform_violations)
    message(FATAL_ERROR
        "Layering audit failed: a graphics API or window system outside its edge\n"
        "${platform_violations}"
        "Draw through nmo_render's backend interface (src/render/render_internal.h); open windows in src/view.\n")
endif()

# The operating system stays behind the platform layer: no source outside
# src/platform includes an operating system's headers (POSIX's, Windows')
# or branches on which system it is built for. A renderer backend may pick
# where its graphics API's headers are, and nothing else.
set(os_violations "")
file(GLOB_RECURSE os_files "${SOURCE_DIR}/src/*.c" "${SOURCE_DIR}/src/*.h")
foreach(os_file IN LISTS os_files)
    file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${os_file}")
    if(relative_path MATCHES "^src/platform/")
        continue()
    endif()
    file(STRINGS "${os_file}" os_lines
        REGEX "^[ \t]*#[ \t]*include[ \t]*<(windows|unistd|dlfcn|fcntl|libgen|strings|dirent|pthread|io|direct|malloc)\\.h>|^[ \t]*#[ \t]*include[ \t]*<(sys|mach)/|^[ \t]*#[ \t]*(if|ifdef|ifndef|elif).*(_WIN32|_WIN64|__APPLE__|__linux__|__unix__|__ANDROID__|__EMSCRIPTEN__|__FreeBSD__|__MINGW32__|_POSIX_C_SOURCE)")
    foreach(os_line IN LISTS os_lines)
        if(relative_path MATCHES "^src/render/[a-z0-9_]+/" AND os_line MATCHES "^[ \t]*#[ \t]*(if|ifdef|ifndef|elif)")
            continue()
        endif()
        string(APPEND os_violations "${relative_path}: ${os_line}\n")
    endforeach()
endforeach()
if(os_violations)
    message(FATAL_ERROR
        "Layering audit failed: the operating system outside the platform layer\n"
        "${os_violations}"
        "Add what is needed to src/platform/platform.h and implement it there.\n")
endif()

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
