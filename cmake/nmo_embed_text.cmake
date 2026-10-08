# cmake -DINPUT=<file> -DOUTPUT=<file> -P nmo_embed_text.cmake
# See nmo_embed_text() in NmoEmbedText.cmake.
file(READ "${INPUT}" _nmo_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _nmo_hex "${_nmo_hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],)(0x[0-9a-f][0-9a-f],)(0x[0-9a-f][0-9a-f],)(0x[0-9a-f][0-9a-f],)(0x[0-9a-f][0-9a-f],)(0x[0-9a-f][0-9a-f],)(0x[0-9a-f][0-9a-f],)(0x[0-9a-f][0-9a-f],))"
       "\\1\n" _nmo_hex "${_nmo_hex}")
file(WRITE "${OUTPUT}" "${_nmo_hex}0x00\n")
