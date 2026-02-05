#include "type/nmo_builtin_type_guids.h"

int nmo_guid_is_field_type(nmo_guid_t guid) {
    if (guid.d1 == 0 && guid.d2 == 0) {
        return 0;
    }
    if ((guid.d1 & NMO_GUID_FIELD_BASE_MASK) != NMO_GUID_FIELD_BASE) {
        return 0;
    }
    return (guid.d1 & 0xFFu) <= NMO_GUID_FIELD_CLASS_CONTAINER;
}

nmo_guid_t nmo_guid_field_to_type(nmo_guid_t guid) {
    if (!nmo_guid_is_field_type(guid)) {
        return guid;
    }
    return (nmo_guid_t){
        .d1 = NMO_TYPE_GUID_BASE | (guid.d1 & 0xFFu),
        .d2 = guid.d2
    };
}
