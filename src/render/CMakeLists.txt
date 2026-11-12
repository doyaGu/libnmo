# Render component: draws the engine's frames (nmo_engine_render_frame) the
# way CK2's rasterizer would, through a backend. The front end (nmo_render)
# uses no graphics API and no window system; each backend is a library of its
# own built on it, so a host links the one it draws with.

set(NMO_RENDER_SOURCES
    render.c
    render_font.c
    render_image.c
    render_light.c
    render_math.c
    render_mesh.c
    render_text.c
)
list(TRANSFORM NMO_RENDER_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_library(nmo_render STATIC ${NMO_RENDER_SOURCES})
target_link_libraries(nmo_render PUBLIC nmo_engine)
target_include_directories(nmo_render PRIVATE "${PROJECT_SOURCE_DIR}/deps")
if(UNIX)
    target_link_libraries(nmo_render PUBLIC m)
endif()
add_library(nmo::render ALIAS nmo_render)

# The OpenGL backend: the fixed function pipeline (OpenGL 2.1 and
# EXT_framebuffer_object), in the context the host made current.
if(NMO_RENDER_GL OR NMO_VIEWER)
    find_package(OpenGL REQUIRED)
    add_library(nmo_render_gl STATIC "${CMAKE_CURRENT_SOURCE_DIR}/gl/render_gl.c")
    target_link_libraries(nmo_render_gl PUBLIC nmo_render PRIVATE OpenGL::GL)
    add_library(nmo::render_gl ALIAS nmo_render_gl)
endif()
