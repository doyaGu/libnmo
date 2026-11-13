# Optional view: a window (SDL3) that shows what the engine's render context
# draws, through nmo_render's OpenGL, GL3, SDL_GPU or software backend
# (whichever the build has; the software one always), and plays the
# engine's voices (SDL3 audio). Built with NMO_VIEWER.

nmo_find_sdl3()

add_library(nmo_view STATIC "${CMAKE_CURRENT_SOURCE_DIR}/view.c"
                            "${CMAKE_CURRENT_SOURCE_DIR}/audio.c")
target_link_libraries(nmo_view PUBLIC nmo_engine PRIVATE nmo_render_soft SDL3::SDL3)
foreach(_nmo_view_backend IN ITEMS nmo_render_gl nmo_render_gl3 nmo_render_gpu)
    if(TARGET ${_nmo_view_backend})
        target_link_libraries(nmo_view PRIVATE ${_nmo_view_backend})
    endif()
endforeach()
target_compile_definitions(nmo_view PUBLIC NMO_HAS_VIEW=1)
add_library(nmo::view ALIAS nmo_view)
