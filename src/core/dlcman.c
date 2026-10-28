#include "dlcman.h"
#include "dlcfmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAN_MAX 32
#define MAN_SIZE (256 * 1024)

void dlc_pack_name(const char *folder, const char *game_title, char *out, size_t cap) {
    char s[256];
    snprintf(s, sizeof s, "%s", folder);
    size_t tl = strlen(game_title);
    // "<game title> - Name": drop the prefix (only with the dash, so "Toy Story 3 (DLC)" stays whole)
    if (tl && strncasecmp(s, game_title, tl) == 0 && strncmp(s + tl, " - ", 3) == 0) memmove(s, s + tl + 3, strlen(s + tl + 3) + 1);
    // trailing "(Europe) (DLC)"-like tags
    size_t n = strlen(s);
    while (n && s[n - 1] == ')') {
        char *open = strrchr(s, '(');
        if (!open || open == s) break;
        char *cut = open;
        while (cut > s && cut[-1] == ' ') cut--;
        if (cut == s) break;
        *cut = 0;
        n = strlen(s);
    }
    // Nothing left, or only the game's own title ("Toy Story 3 (DLC)"): the folder name says more.
    snprintf(out, cap, "%s", s[0] && !dlc_title_eq(s, game_title) ? s : folder);
}

typedef struct { char name[128]; char *files; } Pack;   // files: "a\nb\n"

static int load(const char *manifest, Pack *p, int max) {
    FILE *f = fopen(manifest, "rb");
    if (!f) return 0;
    char *t = malloc(MAN_SIZE + 1);
    size_t len = t ? fread(t, 1, MAN_SIZE, f) : 0;
    fclose(f);
    if (!t) return 0;
    t[len] = 0;
    int n = -1;
    for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
        if (strncmp(line, "pack\t", 5) == 0 && n + 1 < max) {
            n++;
            snprintf(p[n].name, sizeof p[n].name, "%s", line + 5);
            p[n].files = calloc(1, 1);
        } else if (strncmp(line, "file\t", 5) == 0 && n >= 0 && p[n].files) {
            size_t a = strlen(p[n].files), b = strlen(line + 5);
            char *g = realloc(p[n].files, a + b + 2);
            if (!g) continue;
            memcpy(g + a, line + 5, b);
            g[a + b] = '\n';
            g[a + b + 1] = 0;
            p[n].files = g;
        }
    }
    free(t);
    return n + 1;
}

static void free_packs(Pack *p, int n) {
    for (int i = 0; i < n; i++) free(p[i].files);
}

static int save(const char *manifest, const Pack *p, int n) {
    if (n == 0) return remove(manifest) == 0 ? 0 : 0;
    FILE *f = fopen(manifest, "wb");
    if (!f) return -1;
    for (int i = 0; i < n; i++) {
        fprintf(f, "pack\t%s\n", p[i].name);
        for (const char *s = p[i].files; s && *s;) {
            const char *nl = strchr(s, '\n');
            int k = nl ? (int)(nl - s) : (int)strlen(s);
            if (k) fprintf(f, "file\t%.*s\n", k, s);
            s += k + (nl ? 1 : 0);
        }
    }
    return fclose(f) == 0 ? 0 : -1;
}

static int has_line(const char *files, const char *path) {
    size_t n = strlen(path);
    for (const char *s = files; s && *s;) {
        const char *nl = strchr(s, '\n');
        size_t k = nl ? (size_t)(nl - s) : strlen(s);
        if (k == n && strncasecmp(s, path, n) == 0) return 1;
        s += k + (nl ? 1 : 0);
    }
    return 0;
}

int dlcman_load(const char *manifest, const char *game_dir, DlcEntry *out, int max) {
    Pack p[MAN_MAX];
    int n = load(manifest, p, MAN_MAX), k = 0;
    for (int i = 0; i < n && k < max; i++, k++) {
        DlcEntry *e = &out[k];
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", p[i].name);
        for (const char *s = p[i].files; s && *s;) {
            const char *nl = strchr(s, '\n');
            size_t len = nl ? (size_t)(nl - s) : strlen(s);
            char path[512];
            snprintf(path, sizeof path, "%s/%.*s", game_dir, (int)len, s);
            struct stat st;
            e->files++;
            if (stat(path, &st) == 0) e->bytes += st.st_size;
            s += len + (nl ? 1 : 0);
        }
    }
    free_packs(p, n);
    return k;
}

int dlcman_add(const char *manifest, const char *name, const char *files) {
    Pack p[MAN_MAX];
    int n = load(manifest, p, MAN_MAX - 1);
    for (int i = 0; i < n; i++)
        if (strcmp(p[i].name, name) == 0) {   // reinstalled: replace
            free(p[i].files);
            memmove(&p[i], &p[i + 1], sizeof p[0] * (size_t)(n - i - 1));
            n--;
            break;
        }
    snprintf(p[n].name, sizeof p[n].name, "%s", name);
    p[n].files = strdup(files ? files : "");
    n++;
    int rc = save(manifest, p, n);
    free_packs(p, n);
    return rc;
}

static int game_file(const char *rel) {
    const char *b = strrchr(rel, '/');
    b = b ? b + 1 : rel;
    return strcasecmp(b, "EBOOT.PBP") == 0 || strcasecmp(b, "PBOOT.PBP") == 0;
}

// Remove the folders of a path upwards while they are empty (stops at the game folder).
static void prune_dirs(const char *game_dir, const char *rel) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", game_dir, rel);
    for (char *slash = strrchr(p, '/'); slash && (size_t)(slash - p) > strlen(game_dir); slash = strrchr(p, '/')) {
        *slash = 0;
        if (rmdir(p) != 0) break;
    }
}

int dlcman_delete(const char *manifest, const char *game_dir, const int *selected, int n) {
    Pack p[MAN_MAX];
    int count = load(manifest, p, MAN_MAX), deleted = 0;
    for (int i = 0; i < count && i < n; i++) {
        if (!selected[i]) continue;
        for (const char *s = p[i].files; s && *s;) {
            const char *nl = strchr(s, '\n');
            size_t len = nl ? (size_t)(nl - s) : strlen(s);
            char rel[400];
            snprintf(rel, sizeof rel, "%.*s", (int)len, s);
            int shared = 0;
            for (int j = 0; j < count && !shared; j++)
                if (j != i && !(j < n && selected[j]) && has_line(p[j].files, rel)) shared = 1;
            if (!shared && !game_file(rel)) {
                char path[512];
                snprintf(path, sizeof path, "%s/%s", game_dir, rel);
                remove(path);
                prune_dirs(game_dir, rel);
            }
            s += len + (nl ? 1 : 0);
        }
        deleted++;
    }
    int k = 0;
    for (int i = 0; i < count; i++) {
        if (i < n && selected[i]) { free(p[i].files); continue; }
        p[k++] = p[i];
    }
    save(manifest, p, k);
    if (k == 0) rmdir(game_dir);   // only when empty (a PSN game's own files keep it)
    free_packs(p, k);
    return deleted;
}
