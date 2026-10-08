# nmo_embed_text(<input> <output>)
#
# Generate <output> from <input> at build time: the bytes of <input> as a
# comma-separated list of C character constants, then a terminating 0, for
#     static const char text[] = {
#     #include "<output>"
#     };
# A string literal would do, but ISO C caps the length of one.
function(nmo_embed_text input output)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${input}" "-DOUTPUT=${output}"
                -P "${PROJECT_SOURCE_DIR}/cmake/nmo_embed_text.cmake"
        DEPENDS "${input}" "${PROJECT_SOURCE_DIR}/cmake/nmo_embed_text.cmake"
        COMMENT "Embedding ${input}"
        VERBATIM
    )
endfunction()
