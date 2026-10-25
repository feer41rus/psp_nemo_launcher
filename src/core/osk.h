#ifndef NEMO_OSK_H
#define NEMO_OSK_H

#include "input.h"

typedef enum { OSK_EDITING, OSK_DONE, OSK_CANCEL } OskState;
// TEXT: RU/EN letters (folders, playlists). PASSWORD: ASCII only, letters page + symbols page.
typedef enum { OSK_MODE_TEXT, OSK_MODE_PASSWORD } OskMode;

#define OSK_TEXT_MAX 64

// On-screen keyboard model: digits row, three letter rows (RU or EN), special row.
typedef struct {
    int layout;             // 0 = RU, 1 = EN
    int shift;              // capital letters while set (sticky, SHIFT key)
    int hold;               // capital letters while L is held
    int row, col;
    char text[OSK_TEXT_MAX];
    OskMode mode;
    int page;               // password: 0 letters, 1 symbols
    int min_len, max_len;   // DONE allowed only inside (0, 0 = any non-blank text)
    int hidden;             // password shown as bullets (triangle toggles)
} Osk;

void osk_init(Osk *o, const char *initial);              // OSK_MODE_TEXT
void osk_init_mode(Osk *o, const char *initial, OskMode mode);
int  osk_page(const Osk *o);
void osk_set_length_rule(Osk *o, int min_len, int max_len);
int  osk_can_finish(const Osk *o);
int  osk_hidden(const Osk *o);
OskState osk_handle(Osk *o, Action a);
// Capitals while the L trigger is held (does not touch the sticky SHIFT state).
void osk_set_hold_shift(Osk *o, int held);
int osk_rows(void);
int osk_cols(const Osk *o, int row);
// UTF-8 key label; special keys are "SHIFT", "SPACE", "DEL", "LANG", "DONE",
// and in password mode "SYMBOLS" / "LETTERS" instead of "LANG".
const char *osk_key(const Osk *o, int row, int col);

#endif
