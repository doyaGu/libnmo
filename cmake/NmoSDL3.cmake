# SDL3 for the view, the SDL_GPU backend and the web player: Emscripten's
# own port of SDL3 under Emscripten (--use-port=sdl3), else the installed
# package. A macro, so find_package's results land in the caller's scope.
macro(nmo_find_sdl3)
    if(EMSCRIPTEN)
        if(NOT TARGET SDL3::SDL3)
            add_library(SDL3::SDL3 INTERFACE IMPORTED GLOBAL)
            target_compile_options(SDL3::SDL3 INTERFACE --use-port=sdl3)
            # A link item, not a link option, so that it reaches the program
            # through static libraries that link SDL3 privately (nmo_view)
            target_link_libraries(SDL3::SDL3 INTERFACE --use-port=sdl3)
        endif()
    else()
        find_package(SDL3 CONFIG REQUIRED)
    endif()
endmacro()
