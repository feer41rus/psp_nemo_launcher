#include "osk.h"
#include "utf8.h"
#include <string.h>

#define OSK_ROWS 5
#define OSK_MAX_COLS 11

typedef const char *Row[OSK_MAX_COLS + 1];   // NULL-terminated

static const Row DIGITS = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", NULL };
static const Row SPECIAL = { "SHIFT", "SPACE", "DEL", "LANG", "DONE", NULL };

// [layout][shift][row]
static const Row LETTERS[2][2][3] = {
    {   {   { "й","ц","у","к","е","н","г","ш","щ","з","х",NULL },
            { "ф","ы","в","а","п","р","о","л","д","ж","э",NULL },
            { "я","ч","с","м","и","т","ь","б","ю","ё",NULL } },
        {   { "Й","Ц","У","К","Е","Н","Г","Ш","Щ","З","Х",NULL },
            { "Ф","Ы","В","А","П","Р","О","Л","Д","Ж","Э",NULL },
            { "Я","Ч","С","М","И","Т","Ь","Б","Ю","Ё",NULL } } },
    {   {   { "q","w","e","r","t","y","u","i","o","p",NULL },
            { "a","s","d","f","g","h","j","k","l",NULL },
            { "z","x","c","v","b","n","m","-",".",NULL } },
        {   { "Q","W","E","R","T","Y","U","I","O","P",NULL },
            { "A","S","D","F","G","H","J","K","L",NULL },
            { "Z","X","C","V","B","N","M","-",".",NULL } } },
};

// Password mode: ASCII letters and all 32 printable punctuation characters.
static const Row PW_SPECIAL[2] = {
    { "SHIFT", "SPACE", "DEL", "SYMBOLS", "DONE", NULL },
    { "SHIFT", "SPACE", "DEL", "LETTERS", "DONE", NULL },
};
static const Row PW_LETTERS[2][3] = {
    {   { "q","w","e","r","t","y","u","i","o","p",NULL },
        { "a","s","d","f","g","h","j","k","l",NULL },
        { "z","x","c","v","b","n","m","-",".",NULL } },
    {   { "Q","W","E","R","T","Y","U","I","O","P",NULL },
        { "A","S","D","F","G","H","J","K","L",NULL },
        { "Z","X","C","V","B","N","M","-",".",NULL } },
};
static const Row PW_SYMBOLS[3] = {
    { "!","@","#","$","%","^","&","*","(",")","-",NULL },
    { "_","=","+","[","]","{","}","\\","|",";",":",NULL },
    { "'","\"",",",".","<",">","/","?","`","~",NULL },
};

// Full mode: the letters of TEXT, the symbols of PASSWORD.
static const Row FULL_SPECIAL[2] = {
    { "SHIFT", "SPACE", "DEL", "LANG", "SYMBOLS", "DONE", NULL },
    { "SHIFT", "SPACE", "DEL", "LANG", "LETTERS", "DONE", NULL },
};

static size_t text_cap(const Osk *o) { return o->mode == OSK_MODE_FULL ? OSK_TEXT_MAX : OSK_NAME_MAX; }

static const char *const *row_keys(const Osk *o, int row) {
    if (row == 0) return DIGITS;
    if (o->mode == OSK_MODE_FULL) {
        if (row == OSK_ROWS - 1) return FULL_SPECIAL[o->page];
        if (o->page) return PW_SYMBOLS[row - 1];
    } else if (o->mode == OSK_MODE_PASSWORD) {
        if (row == OSK_ROWS - 1) return PW_SPECIAL[o->page];
        if (o->page) return PW_SYMBOLS[row - 1];
        return PW_LETTERS[(o->shift || o->hold) ? 1 : 0][row - 1];
    }
    if (row == OSK_ROWS - 1 && o->mode != OSK_MODE_FULL) return SPECIAL;
    return LETTERS[o->layout ? 1 : 0][(o->shift || o->hold) ? 1 : 0][row - 1];
}

int osk_rows(void) { return OSK_ROWS; }

int osk_cols(const Osk *o, int row) {
    if (row < 0 || row >= OSK_ROWS) return 0;
    const char *const *keys = row_keys(o, row);
    int n = 0;
    while (keys[n]) n++;
    return n;
}

const char *osk_key(const Osk *o, int row, int col) {
    if (col < 0 || col >= osk_cols(o, row)) return "";
    return row_keys(o, row)[col];
}

void osk_init_mode(Osk *o, const char *initial, OskMode mode) {
    memset(o, 0, sizeof *o);
    o->mode = mode;
    utf8_copy(o->text, text_cap(o), initial ? initial : "");
    o->row = 1;
}

void osk_init(Osk *o, const char *initial) { osk_init_mode(o, initial, OSK_MODE_TEXT); }

int osk_page(const Osk *o) { return o->page; }
int osk_hidden(const Osk *o) { return o->hidden; }

void osk_set_length_rule(Osk *o, int min_len, int max_len) {
    o->min_len = min_len;
    o->max_len = max_len;
}

void osk_set_hold_shift(Osk *o, int held) { o->hold = held ? 1 : 0; }

// Same horizontal position in a row of a different length (edges stay edges).
static int map_col(int col, int from_cols, int to_cols) {
    if (to_cols <= 0) return 0;
    if (col >= from_cols - 1) return to_cols - 1;
    int c = col * to_cols / from_cols;
    return c < to_cols ? c : to_cols - 1;
}

static void append(Osk *o, const char *s) {
    size_t have = strlen(o->text), add = strlen(s);
    if (have + add > text_cap(o) - 1) return;   // never split a letter
    if (o->max_len > 0 && have + add > (size_t)o->max_len) return;
    memcpy(o->text + have, s, add + 1);
}

static void backspace(Osk *o) {
    size_t len = strlen(o->text);
    o->text[utf8_prev(o->text, len)] = '\0';
}

static int is_blank(const char *s) {
    for (; *s; s++) if (*s != ' ') return 0;
    return 1;
}

int osk_can_finish(const Osk *o) {
    if (o->min_len > 0 || o->max_len > 0) {
        int len = (int)strlen(o->text);
        return len >= o->min_len && (o->max_len <= 0 || len <= o->max_len);
    }
    return o->mode == OSK_MODE_FULL || !is_blank(o->text);   // an editor line may be empty
}

static OskState press(Osk *o) {
    const char *k = osk_key(o, o->row, o->col);
    if (strcmp(k, "SHIFT") == 0) o->shift = !o->shift;
    else if (strcmp(k, "SPACE") == 0) append(o, " ");
    else if (strcmp(k, "DEL") == 0) backspace(o);
    else if (strcmp(k, "LANG") == 0) o->layout = !o->layout;
    else if (strcmp(k, "SYMBOLS") == 0) o->page = 1;
    else if (strcmp(k, "LETTERS") == 0) o->page = 0;
    else if (strcmp(k, "DONE") == 0) return osk_can_finish(o) ? OSK_DONE : OSK_EDITING;
    else append(o, k);
    return OSK_EDITING;
}

OskState osk_handle(Osk *o, Action a) {
    int cols = osk_cols(o, o->row);
    switch (a) {
    case ACT_UP:
    case ACT_DOWN: {
        int next = (o->row + (a == ACT_UP ? OSK_ROWS - 1 : 1)) % OSK_ROWS;
        o->col = map_col(o->col, cols, osk_cols(o, next));
        o->row = next;
        break;
    }
    case ACT_LEFT: o->col = (o->col + cols - 1) % cols; break;
    case ACT_RIGHT: o->col = (o->col + 1) % cols; break;
    case ACT_QUICK: backspace(o); break;
    case ACT_START: return osk_can_finish(o) ? OSK_DONE : OSK_EDITING;
    case ACT_MENU: if (o->mode == OSK_MODE_PASSWORD) o->hidden = !o->hidden; break;
    case ACT_BACK: return OSK_CANCEL;
    case ACT_CONFIRM: return press(o);
    default: break;
    }
    int max = osk_cols(o, o->row) - 1;
    if (o->col > max) o->col = max;
    return OSK_EDITING;
}
