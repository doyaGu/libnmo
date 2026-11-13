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

# The SDL_GPU backend: Metal, Vulkan or Direct3D 12 through SDL3, with the
# shaders of gpu/shaders compiled ahead of time and checked in
# (tools/scripts/gen_gpu_shaders.py), so building it needs only SDL3.
if(NMO_RENDER_GPU OR (NMO_VIEWER AND NOT EMSCRIPTEN))
    nmo_find_sdl3()
    add_library(nmo_render_gpu STATIC "${CMAKE_CURRENT_SOURCE_DIR}/gpu/render_gpu.c")
    target_link_libraries(nmo_render_gpu PUBLIC nmo_render PRIVATE SDL3::SDL3)
    target_compile_definitions(nmo_render_gpu PUBLIC NMO_HAS_RENDER_GPU=1)
    add_library(nmo::render_gpu ALIAS nmo_render_gpu)
endif()

# The OpenGL backend: the fixed function pipeline (OpenGL 2.1 and
# EXT_framebuffer_object), in the context the host made current. Desktop
# only: WebGL and most GL ES drivers have no fixed function pipeline.
if(NMO_RENDER_GL OR (NMO_VIEWER AND NOT EMSCRIPTEN))
    find_package(OpenGL REQUIRED)
    add_library(nmo_render_gl STATIC "${CMAKE_CURRENT_SOURCE_DIR}/gl/render_gl.c")
    target_link_libraries(nmo_render_gl PUBLIC nmo_render PRIVATE OpenGL::GL)
    target_compile_definitions(nmo_render_gl PUBLIC NMO_HAS_RENDER_GL=1)
    add_library(nmo::render_gl ALIAS nmo_render_gl)
endif()

# The GL3 backend: the same shader as the SDL_GPU backend's (compiled to
# GLSL), on OpenGL 3.3 core, OpenGL ES 3.0 or WebGL 2. No OpenGL headers or
# library: every function is loaded from the current context, so it needs
# only SDL3 (for an offscreen context of its own, in its test).
if(NMO_RENDER_GL3 OR NMO_VIEWER)
    nmo_find_sdl3()
    add_library(nmo_render_gl3 STATIC "${CMAKE_CURRENT_SOURCE_DIR}/gl3/render_gl3.c")
    target_link_libraries(nmo_render_gl3 PUBLIC nmo_render PRIVATE SDL3::SDL3)
    target_compile_definitions(nmo_render_gl3 PUBLIC NMO_HAS_RENDER_GL3=1)
    # The WebGL 2/GLES3 Emscripten settings a program linking this needs
    # (-sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 -sFULL_ES3=1, the last being what
    # registers the GLES3-only entry points VAOs/UBOs/glClearBuffer*/etc. need so
    # SDL_GL_GetProcAddress finds them) do not reach a program through nmo_view's PRIVATE
    # link of this library (CMake's INTERFACE link options do not cross a PRIVATE library
    # dependency the way PRIVATE link *libraries* do); the program itself must set them
    # (apps/web/CMakeLists.txt does, for nmo_player).
    add_library(nmo::render_gl3 ALIAS nmo_render_gl3)
endif()
