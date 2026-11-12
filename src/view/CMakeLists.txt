# Optional view: a window (SDL3) that shows what the engine's render context
# draws, through nmo_render's OpenGL or SDL_GPU backend, and plays the
# engine's voices (SDL3 audio). Built with NMO_VIEWER.

find_package(SDL3 CONFIG REQUIRED)

add_library(nmo_view STATIC "${CMAKE_CURRENT_SOURCE_DIR}/view.c"
                            "${CMAKE_CURRENT_SOURCE_DIR}/audio.c")
target_link_libraries(nmo_view PUBLIC nmo_engine PRIVATE nmo_render_gl nmo_render_gpu SDL3::SDL3)
target_compile_definitions(nmo_view PUBLIC NMO_HAS_VIEW=1)
add_library(nmo::view ALIAS nmo_view)
