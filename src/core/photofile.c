#include "photofile.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static long file_size(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode) ? (long)st.st_size : -1;
}

int photofile_replace(const char *path, PhotoWriteFn write, void *ctx) {
    return photofile_replace_sfx(path, ".tmp", ".bak", write, ctx);
}

int photofile_replace_sfx(const char *path, const char *tmp_sfx, const char *bak_sfx, PhotoWriteFn write, void *ctx) {
    char tmp[600], bak[600];
    snprintf(tmp, sizeof tmp, "%s%s", path, tmp_sfx);
    snprintf(bak, sizeof bak, "%s%s", path, bak_sfx);
    remove(tmp);
    if (write(ctx, tmp) != 0 || file_size(tmp) <= 0) { remove(tmp); return -1; }
    remove(bak);
    if (rename(path, bak) != 0) { remove(tmp); return -1; }
    if (rename(tmp, path) != 0) {   // put the original back
        rename(bak, path);
        remove(tmp);
        return -1;
    }
    remove(bak);
    return 0;
}

int photofile_recover(const char *leftover) {
    size_t n = strlen(leftover);
    if (n > 4 && n < 600 && !strcasecmp(leftover + n - 4, ".bak")) {
        char photo[600];
        memcpy(photo, leftover, n - 4);
        photo[n - 4] = '\0';
        if (file_size(photo) < 0 && rename(leftover, photo) == 0) return 1;
    }
    remove(leftover);
    return 0;
}

static int has_ext(const char *name, const char *ext) {
    size_t n = strlen(name), e = strlen(ext);
    return name[0] != '.' && n > e && strcasecmp(name + n - e, ext) == 0;
}

int photofile_count_ext(const char *dir, const char *ext) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char p[600];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (has_ext(e->d_name, ext) && file_size(p) >= 0) n++;
    }
    closedir(d);
    return n;
}

static int copy_then_delete(const char *from, const char *to) {
    FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
    int ok = in && out;
    static char buf[32 * 1024];
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0) ok = fwrite(buf, 1, n, out) == n;
    if (in) fclose(in);
    if (out && fclose(out) != 0) ok = 0;
    if (ok && file_size(to) == file_size(from)) return remove(from) == 0 ? 0 : -1;
    if (out) remove(to);
    return -1;
}

int photofile_move_old(const char *from_dir, const char *to_dir, const char *ext, int (*mkdir_fn)(const char *)) {
    DIR *d = opendir(from_dir);
    if (!d) return 0;
    if (mkdir_fn) mkdir_fn(to_dir);
    size_t ext_len = strlen(ext);
    int moved = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (e->d_name[0] == '.' || n <= ext_len || strcasecmp(e->d_name + n - ext_len, ext) != 0) continue;
        char from[600], to[700];
        snprintf(from, sizeof from, "%s/%s", from_dir, e->d_name);
        struct stat st;
        if (stat(from, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        snprintf(to, sizeof to, "%s/%s", to_dir, e->d_name);
        for (int k = 2; file_size(to) >= 0 && k < 1000; k++)   // taken: <stem>_k<ext>
            snprintf(to, sizeof to, "%s/%.*s_%d%s", to_dir, (int)(n - ext_len), e->d_name, k, e->d_name + n - ext_len);
        if (file_size(to) < 0 && (rename(from, to) == 0 || copy_then_delete(from, to) == 0)) moved++;
    }
    closedir(d);
    return moved;
}
