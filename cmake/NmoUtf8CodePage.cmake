# nmo_use_utf8_code_page(<target>)
#
# A Windows program of libnmo runs with the UTF-8 code page: its command line,
# the C library's file calls and the libraries it uses (Lua's io.open, say)
# take UTF-8, as libnmo's paths are. It needs Windows 10 1903 or later; older
# Windows ignore it and keep the ANSI code page. Elsewhere it does nothing.
#
# MSVC merges cmake/nmo_utf8.manifest into the one its linker makes (which then
# leaves out its own UAC part, the manifest has it); MinGW links it as the
# program's manifest resource.

set(NMO_UTF8_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/nmo_utf8.manifest")

if(MINGW)
    enable_language(RC)
    set(NMO_UTF8_MANIFEST_RC "${CMAKE_BINARY_DIR}/nmo_utf8_manifest.rc")
    # 1 = CREATEPROCESS_MANIFEST_RESOURCE_ID, 24 = RT_MANIFEST
    file(WRITE "${NMO_UTF8_MANIFEST_RC}" "1 24 \"${NMO_UTF8_MANIFEST}\"\n")
endif()

function(nmo_use_utf8_code_page target)
    if(MSVC)
        target_sources(${target} PRIVATE "${NMO_UTF8_MANIFEST}")
        target_link_options(${target} PRIVATE /MANIFESTUAC:NO)
    elseif(MINGW)
        target_sources(${target} PRIVATE "${NMO_UTF8_MANIFEST_RC}")
    endif()
endfunction()
