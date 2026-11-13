# nmo's player (player.c): a document run in the engine as a game player
# runs it. Under Emscripten it is the web player: nmo-player.mjs and .wasm,
# libnmo's data files packed in nmo-player.data, and the page (index.html,
# player.mjs, player.css) that copies the player's game folder into memory
# and starts it. Elsewhere it opens a window (with NMO_VIEWER) or runs
# headless.

add_executable(nmo_player player.c "${PROJECT_SOURCE_DIR}/tools/nmo_run_host.c")
target_include_directories(nmo_player PRIVATE "${PROJECT_SOURCE_DIR}/tools")
target_link_libraries(nmo_player PRIVATE nmo_bb_virtools nmo_bb_physics nmo_bb_terratools
                      nmo_render_soft nmo_engine nmo)
if(NMO_BUILD_SHARED)
    target_sources(nmo_player PRIVATE "${PROJECT_SOURCE_DIR}/${NMO_PLATFORM_SOURCES}")
endif()
foreach(_nmo_player_part IN ITEMS nmo_view nmo_physics_ivp)
    if(TARGET ${_nmo_player_part})
        target_link_libraries(nmo_player PRIVATE ${_nmo_player_part})
    endif()
endforeach()

if(EMSCRIPTEN)
    set_target_properties(nmo_player PROPERTIES OUTPUT_NAME nmo-player SUFFIX ".mjs")
    # libnmo's data files, at the path player.c gives the context
    file(GLOB _nmo_player_data CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/data/virtools_*.json")
    set(_nmo_player_preload)
    foreach(_nmo_player_file IN LISTS _nmo_player_data)
        get_filename_component(_nmo_player_name "${_nmo_player_file}" NAME)
        list(APPEND _nmo_player_preload
             "SHELL:--preload-file ${_nmo_player_file}@/nmo/data/${_nmo_player_name}")
    endforeach()
    # A module the page (and run_node.mjs) creates and calls main in once the
    # game's files are in its file system; IDBFS keeps the saves.
    target_link_options(nmo_player PRIVATE
        -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createNmoPlayer
        -sINVOKE_RUN=0 -sENVIRONMENT=web,node
        -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,callMain
        -sEXPORTED_FUNCTIONS=_main,_player_frames,_player_exit_code
        # WebGL 2 / GLES3 for the GL3 backend (nmo_render_gl3): see src/render/CMakeLists.txt's
        # comment on why these can't just be nmo_render_gl3's own INTERFACE link options.
        -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 -sFULL_ES3=1
        -lidbfs.js
        ${_nmo_player_preload})
    set_property(TARGET nmo_player APPEND PROPERTY LINK_DEPENDS ${_nmo_player_data})
    foreach(_nmo_player_page IN ITEMS index.html player.mjs player.css run_node.mjs)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_nmo_player_page}"
                       "${CMAKE_CURRENT_BINARY_DIR}/${_nmo_player_page}" COPYONLY)
    endforeach()
else()
    set_target_properties(nmo_player PROPERTIES OUTPUT_NAME nmo-player)
    target_compile_definitions(nmo_player PRIVATE PLAYER_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
endif()
