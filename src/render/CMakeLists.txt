# Render component: draws the engine's frames (nmo_engine_render_frame) the
# way CK2's rasterizer would, through a backend. The front end (nmo_render)
# uses no graphics API and no window system; each backend is a library of its
# own built on it, so a host links the one it draws with.

set(NMO_RENDER_SOURCES
    render.c
    render_draw.c
    render_font.c
    render_frame.c
    render_image.c
    render_light.c
    render_math.c
    render_mesh.c
    render_text.c
    render_texture.c
)
list(TRANSFORM NMO_RENDER_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_library(nmo_render STATIC ${NMO_RENDER_SOURCES})
target_link_libraries(nmo_render PUBLIC nmo_engine)
target_include_directories(nmo_render PRIVATE "${PROJECT_SOURCE_DIR}/deps")
if(UNIX)
    target_link_libraries(nmo_render PUBLIC m)
endif()
add_library(nmo::render ALIAS nmo_render)

# The software backend: a rasterizer in C, drawing into memory. It needs no
# device and no window, so it builds wherever the front end does.
add_library(nmo_render_soft STATIC "${CMAKE_CURRENT_SOURCE_DIR}/soft/render_soft.c"
                                   "${CMAKE_CURRENT_SOURCE_DIR}/soft/soft_raster.c")
target_link_libraries(nmo_render_soft PUBLIC nmo_render)
target_compile_definitions(nmo_render_soft PUBLIC NMO_HAS_RENDER_SOFT=1)
add_library(nmo::render_soft ALIAS nmo_render_soft)

# The SDL_GPU backend: Metal or Vulkan through SDL3 (no DXIL yet), with the
# shaders of gpu/shaders compiled ahead of time and checked in
# (tools/scripts/gen_gpu_shaders.py), so building it needs only SDL3.
if(NMO_RENDER_GPU OR NMO_VIEWER)
    find_package(SDL3 CONFIG REQUIRED)
    add_library(nmo_render_gpu STATIC "${CMAKE_CURRENT_SOURCE_DIR}/gpu/render_gpu.c")
    target_link_libraries(nmo_render_gpu PUBLIC nmo_render PRIVATE SDL3::SDL3)
    target_compile_definitions(nmo_render_gpu PUBLIC NMO_HAS_RENDER_GPU=1)
    add_library(nmo::render_gpu ALIAS nmo_render_gpu)
endif()

# The OpenGL backend: the fixed function pipeline (OpenGL 2.1 and
# EXT_framebuffer_object), in the context the host made current.
if(NMO_RENDER_GL OR NMO_VIEWER)
    find_package(OpenGL REQUIRED)
    add_library(nmo_render_gl STATIC "${CMAKE_CURRENT_SOURCE_DIR}/gl/render_gl.c")
    target_link_libraries(nmo_render_gl PUBLIC nmo_render PRIVATE OpenGL::GL)
    add_library(nmo::render_gl ALIAS nmo_render_gl)
endif()
