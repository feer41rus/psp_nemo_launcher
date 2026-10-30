#include "organizer.h"
#include <stddef.h>

StrId org_label(OrgItem it) {
    static const StrId LABELS[ORG_COUNT] = { S_ORG_GALLERY, S_ORG_FILES, S_ORG_VIDEO, S_ORG_CAMERA };
    return (unsigned)it < ORG_COUNT ? LABELS[it] : S_ORG_GALLERY;
}

const char *org_soon_version(OrgItem it) {
    (void)it;   // 1.3.6: every section is in
    return NULL;
}

int org_move(int sel, int delta) { return ((sel + delta) % ORG_COUNT + ORG_COUNT) % ORG_COUNT; }
