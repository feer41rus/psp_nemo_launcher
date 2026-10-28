#include "dlcinst.h"
#include "dlcfmt.h"
#include "dlcman.h"
#include "sfo.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_DEPTH 6
#define PBP_MAX (512 * 1024)

static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static long file_size(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && !S_ISDIR(st.st_mode) ? (long)st.st_size : -1;
}

static uint8_t *read_all(const char *p, size_t max, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    uint8_t *b = malloc(max);
    *n = b ? fread(b, 1, max, f) : 0;
    fclose(f);
    return b;
}

// PARAM.PBP of a folder (any case): name into out; 0 found.
static int find_param(const char *dir, char *out, size_t cap) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    int rc = -1;
    while ((e = readdir(d)) != NULL)
        if (strcasecmp(e->d_name, "PARAM.PBP") == 0) {
            snprintf(out, cap, "%s/%s", dir, e->d_name);
            rc = 0;
            break;
        }
    closedir(d);
    return rc;
}

// CATEGORY / DISC_ID / TITLE of a DLC PARAM.PBP; 0 if it is one (CATEGORY MG).
static int read_dlc_param(const char *path, char *id, size_t idcap, char *title, size_t tcap) {
    size_t n = 0, off, len;
    uint8_t *b = read_all(path, PBP_MAX, &n);
    int rc = -1;
    char cat[8];
    if (b && dlc_pbp_sfo(b, n, &off, &len) == 0 && sfo_get_string(b + off, len, "CATEGORY", cat, sizeof cat) == 0 &&
        strcmp(cat, "MG") == 0 && sfo_get_string(b + off, len, "DISC_ID", id, idcap) == 0) {
        if (sfo_get_string(b + off, len, "TITLE", title, tcap) != 0) title[0] = 0;
        rc = 0;
    }
    free(b);
    return rc;
}

// Any encrypted file in the tree whose licence is missing.
static int needs_licence(const char *dir, const char *license_dir, int depth) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[512];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (is_dir(p)) {
            if (depth < MAX_DEPTH) found = needs_licence(p, license_dir, depth + 1);
            continue;
        }
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        uint8_t h[0x100];
        size_t n = fread(h, 1, sizeof h, f);
        fclose(f);
        char cid[37], rif[512];
        if (dlc_encrypted(h, n, cid)) {
            snprintf(rif, sizeof rif, "%s/%s.rif", license_dir, cid);
            found = !cid[0] || file_size(rif) < 0;
        }
    }
    closedir(d);
    return found;
}

typedef struct {
    const char *license_dir, *game_id, *game_title;
    DlcPack *out;
    int max, n;
} FindCtx;

static void walk(FindCtx *c, const char *dir, const char *top, int depth) {
    char param[512], id[16], title[160];
    if (depth > 0 && find_param(dir, param, sizeof param) == 0 && read_dlc_param(param, id, sizeof id, title, sizeof title) == 0) {
        int same = strcasecmp(id, c->game_id) == 0;
        if ((same || dlc_title_eq(title, c->game_title)) && c->n < c->max) {
            DlcPack *p = &c->out[c->n++];
            memset(p, 0, sizeof *p);
            snprintf(p->dir, sizeof p->dir, "%s", dir);
            snprintf(p->pack, sizeof p->pack, "%s", top);
            snprintf(p->disc_id, sizeof p->disc_id, "%s", id);
            p->match = same ? DLC_SAME : DLC_OTHER;
            p->encrypted = needs_licence(dir, c->license_dir, 0);
        }
        return;   // a pack: nothing more inside
    }
    if (depth >= MAX_DEPTH) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[512];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (!is_dir(p)) continue;
        char name[256];  // a copy: readdir's buffer is not ours to keep
        const char *src = depth == 0 ? e->d_name : top;
        size_t k = strlen(src) < sizeof name - 1 ? strlen(src) : sizeof name - 1;
        memcpy(name, src, k);
        name[k] = 0;
        walk(c, p, name, depth + 1);
    }
    closedir(d);
}

static int by_dir(const void *a, const void *b) { return strcmp(((const DlcPack *)a)->dir, ((const DlcPack *)b)->dir); }

int dlc_find(const char *root, const char *license_dir, const char *game_id, const char *game_title, DlcPack *out,
             int max) {
    if (!game_id || !game_id[0]) return 0;
    FindCtx c = { license_dir, game_id, game_title ? game_title : "", out, max, 0 };
    walk(&c, root, "", 0);
    qsort(out, (size_t)c.n, sizeof *out, by_dir);   // the same order every time: later packs win on clashes
    return c.n;
}

// ---------- install ----------

typedef struct {
    DlcProgress cb;
    void *ctx;
    long long total, done;
    char *files;            // paths copied for the current pack ("a\nb\n"), for the manifest
    size_t files_len, files_cap;
} Prog;

static void note_file(Prog *pr, const char *rel) {
    size_t n = strlen(rel);
    if (pr->files_len + n + 2 > pr->files_cap) {
        size_t cap = (pr->files_cap + n + 2) * 2;
        char *g = realloc(pr->files, cap);
        if (!g) return;
        pr->files = g;
        pr->files_cap = cap;
    }
    memcpy(pr->files + pr->files_len, rel, n);
    pr->files_len += n;
    pr->files[pr->files_len++] = '\n';
    pr->files[pr->files_len] = 0;
}

static long long tree_bytes(const char *dir, int depth) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    long long sum = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[512];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (is_dir(p)) { if (depth < MAX_DEPTH) sum += tree_bytes(p, depth + 1); }
        else { long s = file_size(p); if (s > 0) sum += s; }
    }
    closedir(d);
    return sum;
}

static int mkdir_p(const char *path) {
    char p[512];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            if (!is_dir(p) && strchr(p, '/')) mkdir(p, 0777);
            *s = '/';
        }
    if (!is_dir(p)) mkdir(p, 0777);
    return is_dir(p) ? 0 : -1;
}

// Same size and bytes: 1.
static int same_file(const char *a, const char *b) {
    long sa = file_size(a), sb = file_size(b);
    if (sa < 0 || sa != sb) return 0;
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    int same = fa && fb;
    static uint8_t x[16384], y[16384];
    while (same) {
        size_t na = fread(x, 1, sizeof x, fa), nb = fread(y, 1, sizeof y, fb);
        if (na != nb || memcmp(x, y, na) != 0) same = 0;
        if (na < sizeof x) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

static int copy_file(const char *src, const char *dst, Prog *pr) {
    long size = file_size(src);
    if (size < 0) return -1;
    if (!same_file(src, dst)) {
        FILE *in = fopen(src, "rb"), *out = in ? fopen(dst, "wb") : NULL;
        if (!in || !out) {
            if (in) fclose(in);
            return -1;
        }
        static uint8_t buf[64 * 1024];
        size_t n;
        int ok = 1;
        while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0) ok = fwrite(buf, 1, n, out) == n;
        fclose(in);
        if (fclose(out) != 0) ok = 0;
        if (!ok || file_size(dst) != size) return -1;   // checked before the source may go
    }
    pr->done += size;
    if (pr->cb) {
        const char *slash = strrchr(src, '/');
        pr->cb(pr->ctx, pr->total > 0 ? (int)(pr->done * 100 / pr->total) : 100, slash ? slash + 1 : src);
    }
    return 0;
}

static int copy_tree(const char *src, const char *dst, const char *rel, Prog *pr, int depth) {
    if (mkdir_p(dst) != 0) return -1;
    DIR *d = opendir(src);
    if (!d) return -1;
    struct dirent *e;
    int rc = 0;
    while (rc == 0 && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char s[512], t[512];
        snprintf(s, sizeof s, "%s/%s", src, e->d_name);
        snprintf(t, sizeof t, "%s/%s", dst, e->d_name);
        char r[400];
        snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        if (is_dir(s)) rc = depth < MAX_DEPTH ? copy_tree(s, t, r, pr, depth + 1) : -1;
        else if ((rc = copy_file(s, t, pr)) == 0) note_file(pr, r);
    }
    closedir(d);
    return rc;
}

static void remove_tree(const char *dir, int depth) {
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            char p[512];
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            if (is_dir(p)) { if (depth < MAX_DEPTH + 2) remove_tree(p, depth + 1); }
            else remove(p);
        }
        closedir(d);
    }
    rmdir(dir);
}

// DISC_ID of the installed PARAM.PBP := the game's (a DLC for another version of the game).
static int adapt_param(const char *game_dir, const char *game_id) {
    char path[512];
    if (find_param(game_dir, path, sizeof path) != 0) return -1;
    size_t n = 0, off, len;
    uint8_t *b = read_all(path, PBP_MAX, &n);
    int rc = -1;
    if (b && dlc_pbp_sfo(b, n, &off, &len) == 0 && sfo_set_string(b + off, len, "DISC_ID", game_id) == 0) {
        FILE *f = fopen(path, "wb");
        rc = f && fwrite(b, 1, n, f) == n ? 0 : -1;
        if (f && fclose(f) != 0) rc = -1;
    }
    free(b);
    return rc;
}

int dlc_install(const DlcPack *packs, int n, const char *root, const char *game_dir, const char *game_id,
                const char *manifest, const char *game_title, DlcProgress cb, void *ctx) {
    Prog pr = { cb, ctx, 0, 0, NULL, 0, 0 };
    for (int i = 0; i < n; i++)
        if (!packs[i].encrypted) pr.total += tree_bytes(packs[i].dir, 0);
    int installed = 0;
    for (int i = 0; i < n; i++) {
        if (packs[i].encrypted) continue;
        pr.files_len = 0;
        if (pr.files) pr.files[0] = 0;
        if (copy_tree(packs[i].dir, game_dir, "", &pr, 0) != 0) continue;   // the pack stays for another try
        if (packs[i].match == DLC_OTHER && adapt_param(game_dir, game_id) != 0) continue;
        if (manifest) {
            char name[128];
            dlc_pack_name(packs[i].pack[0] ? packs[i].pack : packs[i].disc_id, game_title ? game_title : "", name, sizeof name);
            dlcman_add(manifest, name, pr.files ? pr.files : "");
        }
        char top[512];
        snprintf(top, sizeof top, "%s/%s", root, packs[i].pack[0] ? packs[i].pack : "");
        remove_tree(packs[i].pack[0] ? top : packs[i].dir, 0);
        installed++;
    }
    free(pr.files);
    return installed ? installed : -1;
}

int dlc_installed(const char *game_dir) {
    char path[512], id[16], title[160];
    return find_param(game_dir, path, sizeof path) == 0 && read_dlc_param(path, id, sizeof id, title, sizeof title) == 0;
}
