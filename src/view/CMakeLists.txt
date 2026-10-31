# Optional view: a window that draws what the engine's render context would
# (SDL3 and OpenGL's fixed function pipeline), and plays the engine's
# voices (SDL3 audio). Built with NMO_VIEWER.

find_package(SDL3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_library(nmo_view STATIC "${CMAKE_CURRENT_SOURCE_DIR}/view.c"
                            "${CMAKE_CURRENT_SOURCE_DIR}/audio.c")
target_link_libraries(nmo_view PUBLIC nmo_engine PRIVATE SDL3::SDL3 OpenGL::GL)
target_compile_definitions(nmo_view PUBLIC NMO_HAS_VIEW=1)
add_library(nmo::view ALIAS nmo_view)
