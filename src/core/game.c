#include "game.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *const UTILITY_FOLDERS[] = {
    "CustomIPL", "DC10", "FasterARK", "ARK150on660", "PROMODFW", "UPDATE",
};

void gamelist_init(GameList *l) { memset(l, 0, sizeof *l); }

Game *gamelist_push(GameList *l) {
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 32;
        Game *items = realloc(l->items, (size_t)cap * sizeof *items);
        if (!items) return NULL;
        l->items = items;
        l->cap = cap;
    }
    Game *g = &l->items[l->count++];
    memset(g, 0, sizeof *g);
    return g;
}

void gamelist_free(GameList *l) {
    free(l->items);
    memset(l, 0, sizeof *l);
}

int gamelist_find(const GameList *l, const char *id) {
    for (int i = 0; i < l->count; i++)
        if (strcmp(l->items[i].id, id) == 0) return i;
    return -1;
}

static int cmp_title(const void *a, const void *b) {
    const Game *x = a, *y = b;
    int c = strcasecmp(x->title, y->title);
    return c ? c : strcmp(x->id, y->id);   // deterministic for equal titles
}

void gamelist_sort_by_title(GameList *l) {
    if (l->count > 1) qsort(l->items, (size_t)l->count, sizeof *l->items, cmp_title);
}

GameKind game_kind_for(GameFormat fmt, const char *folder_name) {
    if (fmt == GF_PBP_HOMEBREW) {
        for (size_t i = 0; i < sizeof UTILITY_FOLDERS / sizeof *UTILITY_FOLDERS; i++)
            if (strcasecmp(folder_name, UTILITY_FOLDERS[i]) == 0) return GK_UTILITY;
        return GK_HOMEBREW;
    }
    return fmt == GF_PBP_PS1 ? GK_PS1 : GK_PSP;
}

// Finds "disc" followed by optional spaces/separators and a number; returns start or NULL.
static const char *find_disc_word(const char *s, int *number) {
    for (const char *p = s; *p; p++) {
        if (strncasecmp(p, "disc", 4) != 0) continue;
        if (p > s && isalpha((unsigned char)p[-1])) continue;
        const char *q = p + 4;
        while (*q == ' ' || *q == '_' || *q == '-' || *q == '#') q++;
        if (!isdigit((unsigned char)*q)) continue;
        int n = 0;
        while (isdigit((unsigned char)*q)) n = n * 10 + (*q++ - '0');
        if (n < 1 || n > GAME_MAX_DISCS) continue;
        *number = n;
        return p;
    }
    return NULL;
}

int disc_parse_number(const char *title, const char *folder) {
    int n = 0;
    if (title && find_disc_word(title, &n)) return n;
    if (folder && find_disc_word(folder, &n)) return n;
    return 0;
}

void title_strip_disc_tag(char *title) {
    int n;
    const char *w = find_disc_word(title, &n);
    if (!w) return;
    char *start = (char *)w, *end = (char *)w;
    // Remove the whole bracket group around the tag, e.g. "[Disc1of3]" or "(Disc 2)".
    char *open = NULL;
    for (char *p = start; p >= title; p--) {
        if (*p == '[' || *p == '(') { open = p; break; }
        if (*p == ']' || *p == ')') break;
    }
    if (open) {
        char close = *open == '[' ? ']' : ')';
        char *c = strchr(start, close);
        start = open;
        end = c ? c + 1 : start + strlen(start);
    } else {
        end = start + 4;
        while (*end && (isdigit((unsigned char)*end) || *end == ' ' ||
                        strncasecmp(end, "of", 2) == 0)) end += (strncasecmp(end, "of", 2) == 0) ? 2 : 1;
        while (start > title && (start[-1] == ' ' || start[-1] == '-')) start--;
    }
    memmove(start, end, strlen(end) + 1);
    size_t len = strlen(title);
    while (len > 0 && (title[len - 1] == ' ' || title[len - 1] == '-')) title[--len] = '\0';
}

static const char *folder_of(const char *path, char *buf, size_t cap) {
    // ".../GAME/<folder>/EBOOT.PBP" -> "<folder>"
    const char *end = strrchr(path, '/');
    if (!end) { buf[0] = '\0'; return buf; }
    const char *start = end;
    while (start > path && start[-1] != '/') start--;
    size_t n = (size_t)(end - start);
    if (n >= cap) n = cap - 1;
    memcpy(buf, start, n);
    buf[n] = '\0';
    return buf;
}

static int disc_of(const Game *g) {
    char folder[256];
    return disc_parse_number(g->title, folder_of(g->path, folder, sizeof folder));
}

void games_group_discs(GameList *l) {
    for (int i = 0; i < l->count; i++) {
        Game *head = &l->items[i];
        if (head->format != GF_PBP_PS1 || !head->disc_id[0] || head->disc_count) continue;
        int members[GAME_MAX_DISCS], numbers[GAME_MAX_DISCS], m = 0, ok = 1;
        for (int j = i; j < l->count && ok; j++) {
            Game *g = &l->items[j];
            if (g->format != GF_PBP_PS1 || g->disc_count || strcmp(g->disc_id, head->disc_id) != 0)
                continue;
            int n = disc_of(g);
            if (n == 0 || m == GAME_MAX_DISCS) { ok = 0; break; }
            for (int k = 0; k < m; k++) if (numbers[k] == n) ok = 0;
            members[m] = j;
            numbers[m++] = n;
        }
        if (!ok || m < 2) continue;

        // Order members by disc number; build the merged entry in place of the first member.
        for (int a = 0; a < m; a++)
            for (int b = a + 1; b < m; b++)
                if (numbers[b] < numbers[a]) {
                    int t = numbers[a]; numbers[a] = numbers[b]; numbers[b] = t;
                    t = members[a]; members[a] = members[b]; members[b] = t;
                }
        Game merged = l->items[members[0]];
        for (int k = 0; k < m; k++) {
            strcpy(merged.discs[k].path, l->items[members[k]].path);
            merged.discs[k].number = numbers[k];
        }
        merged.disc_count = m;
        strcpy(merged.path, merged.discs[0].path);
        title_strip_disc_tag(merged.title);
        strcpy(merged.id, "disc:");
        strncat(merged.id, merged.disc_id, sizeof merged.id - 6);

        int target = i;   // the scan starts at i, so i is always a member
        l->items[target] = merged;
        // Remove the other members (highest index first keeps indices valid).
        for (int a = 0; a < m; a++)
            for (int b = a + 1; b < m; b++)
                if (members[b] > members[a]) { int t = members[a]; members[a] = members[b]; members[b] = t; }
        for (int k = 0; k < m; k++) {
            int idx = members[k];
            if (idx == target) continue;
            memmove(&l->items[idx], &l->items[idx + 1], (size_t)(l->count - idx - 1) * sizeof *l->items);
            l->count--;
        }
        i = -1;   // restart: indices shifted
    }
}
