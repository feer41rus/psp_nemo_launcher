#ifndef NEMO_ORGANIZER_H
#define NEMO_ORGANIZER_H

#include "i18n.h"

// The Organizer tab (1.3.2): rows like the Settings menu, each opens its own window.
typedef enum { ORG_GALLERY, ORG_FILES, ORG_VIDEO, ORG_CAMERA, ORG_COUNT } OrgItem;

StrId org_label(OrgItem it);
const char *org_soon_version(OrgItem it);   // NULL for a ready section, else the version it comes in
int org_move(int sel, int delta);           // wraps round

#endif
