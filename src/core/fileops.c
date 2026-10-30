#include "fileops.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

int fileops_test_no_rename;
const char *fileops_test_skip_name;

static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static int do_rename(const char *from, const char *to) {
    return fileops_test_no_rename ? -1 : rename(from, to);
}

// Walks a tree: counts files and bytes.
static void count(const char *path, int *files, long long *bytes) {
    struct stat st;
    if (stat(path, &st) != 0) return;
    if (!S_ISDIR(st.st_mode)) {
        (*files)++;
        *bytes += (long long)st.st_size;
        return;
    }
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[FS_PATH_MAX];
        if (fs_join(p, sizeof p, path, e->d_name) == 0) count(p, files, bytes);
    }
    closedir(d);
}

int fileop_dir_stats(const char *dir, int *files, long long *bytes) {
    *files = 0;
    *bytes = 0;
    if (!exists(dir)) return -1;
    count(dir, files, bytes);
    return 0;
}

static int dest_of(const FileJob *j, int i, char *out, size_t cap) { return fs_join(out, cap, j->dest, fs_name(j->src[i])); }

// Pasting an item where it already is: nothing to do.
static int same_place(const FileJob *j, int i) {
    char to[FS_PATH_MAX];
    dest_of(j, i, to, sizeof to);
    return strcasecmp(to, j->src[i]) == 0;
}

int fileop_plan(FileJob *j) {
    int files = 0;
    long long bytes = 0;
    for (int i = 0; i < j->n_src; i++) {
        if (j->kind != FOP_DELETE && !same_place(j, i)) {
            char to[FS_PATH_MAX];
            if (dest_of(j, i, to, sizeof to) != 0) return FOPE_FAIL;
            // Into itself, or over a folder that holds it (ms0:/A/A pasted into ms0:/ would replace ms0:/A).
            if ((is_dir(j->src[i]) && fs_inside(j->src[i], j->dest)) || fs_inside(to, j->src[i])) return FOPE_INTO_SELF;
        }
        count(j->src[i], &files, &bytes);
    }
    j->files_total = files;
    j->bytes_total = bytes;
    j->files_done = 0;
    j->bytes_done = 0;
    j->failed = 0;
    j->cancel = 0;
    return FOPE_OK;
}

int fileop_conflicts(const FileJob *j, char *first, size_t cap) {
    int n = 0;
    if (cap) first[0] = '\0';
    if (j->kind == FOP_DELETE) return 0;
    for (int i = 0; i < j->n_src; i++) {
        char to[FS_PATH_MAX];
        if (same_place(j, i) || dest_of(j, i, to, sizeof to) != 0 || !exists(to)) continue;
        if (!n && cap) snprintf(first, cap, "%s", fs_name(j->src[i]));
        n++;
    }
    return n;
}

long long fileop_space_needed(const FileJob *j) {
    if (j->kind == FOP_DELETE) return 0;
    long long need = 0;
    for (int i = 0; i < j->n_src; i++) {
        if (j->kind == FOP_MOVE && fs_same_device(j->src[i], j->dest)) continue;
        char to[FS_PATH_MAX];
        struct stat st;
        if (j->on_conflict == FC_SKIP && dest_of(j, i, to, sizeof to) == 0 && stat(to, &st) == 0) continue;
        int f = 0;
        long long b = 0;
        count(j->src[i], &f, &b);
        need += b;
    }
    return need;
}

// Deletes a file or a whole tree; counts files when the job counts deletions.
static int remove_tree(FileJob *j, const char *path, int counted) {
    if (!is_dir(path)) {
        int rc = remove(path);
        if (counted) j->files_done++;
        return rc;
    }
    int rc = 0;
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (counted && j->cancel) break;
            char p[FS_PATH_MAX];
            if (fs_join(p, sizeof p, path, e->d_name) != 0 || remove_tree(j, p, counted) != 0) rc = -1;
        }
        closedir(d);
    }
    if (counted && j->cancel) return FOPE_CANCELLED;
    return rmdir(path) == 0 ? rc : -1;
}

static int copy_file(FileJob *j, const char *from, const char *to, char *buf, size_t cap) {
    FILE *in = fopen(from, "rb");
    if (!in) return FOPE_FAIL;
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); return FOPE_FAIL; }
    int rc = FOPE_OK;
    size_t n;
    while ((n = fread(buf, 1, cap, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { rc = FOPE_FAIL; break; }
        j->bytes_done += (long long)n;
        if (j->on_chunk) j->on_chunk(j);
        if (j->cancel) { rc = FOPE_CANCELLED; break; }
    }
    if (rc == FOPE_OK && ferror(in)) rc = FOPE_FAIL;
    fclose(in);
    if (fclose(out) != 0 && rc == FOPE_OK) rc = FOPE_FAIL;
    if (rc == FOPE_OK) {
        struct stat a, b;
        if (stat(from, &a) != 0 || stat(to, &b) != 0 || a.st_size != b.st_size) rc = FOPE_FAIL;
    }
    if (rc != FOPE_OK) remove(to);
    else j->files_done++;
    return rc;
}

static int copy_tree(FileJob *j, const char *from, const char *to, char *buf, size_t cap) {
    if (!is_dir(from)) return copy_file(j, from, to, buf, cap);
    if (!is_dir(to) && mkdir(to, 0777) != 0) return FOPE_FAIL;
    DIR *d = opendir(from);
    if (!d) return FOPE_FAIL;
    int rc = FOPE_OK;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (fileops_test_skip_name && !strcmp(e->d_name, fileops_test_skip_name)) continue;
        char a[FS_PATH_MAX], b[FS_PATH_MAX];
        int r = fs_join(a, sizeof a, from, e->d_name) == 0 && fs_join(b, sizeof b, to, e->d_name) == 0
                    ? copy_tree(j, a, b, buf, cap) : FOPE_FAIL;
        if (r == FOPE_CANCELLED) { rc = r; break; }
        if (r != FOPE_OK) rc = FOPE_FAIL;
    }
    closedir(d);
    return rc;
}

static void count_done(FileJob *j, const char *path) {
    int f = 0;
    long long b = 0;
    count(path, &f, &b);
    j->files_done += f;
    j->bytes_done += b;
}

// Puts src at `to` (copy, or a rename for a move on one device). The item is complete or absent:
// a failed or cancelled copy removes what it wrote.
static int place(FileJob *j, const char *src, const char *to, char *buf, size_t cap) {
    if (j->kind == FOP_MOVE && fs_same_device(src, to) && do_rename(src, to) == 0) {
        count_done(j, to);
        return FOPE_OK;
    }
    int rc = copy_tree(j, src, to, buf, cap);
    if (rc == FOPE_OK) {   // a listing cut short by a card error copies less without an error: compare
        int fs = 0, ft = 0;
        long long bs = 0, bt = 0;
        count(src, &fs, &bs);
        count(to, &ft, &bt);
        if (fs != ft || bs != bt) rc = FOPE_FAIL;
    }
    if (rc != FOPE_OK) remove_tree(j, to, 0);
    return rc;
}

static int run_one(FileJob *j, int i, char *buf, size_t cap) {
    const char *src = j->src[i];
    if (j->kind == FOP_DELETE) return remove_tree(j, src, 1) == 0 ? FOPE_OK : j->cancel ? FOPE_CANCELLED : FOPE_FAIL;
    if (same_place(j, i)) return FOPE_OK;
    char to[FS_PATH_MAX];
    if (dest_of(j, i, to, sizeof to) != 0) return FOPE_FAIL;
    int rc;
    if (!exists(to)) {
        rc = place(j, src, to, buf, cap);
    } else if (j->on_conflict == FC_SKIP) {
        count_done(j, src);
        return FOPE_OK;
    } else {
        // Replace: the new one is made beside the old, which goes only once the new one is whole.
        char tmp[FS_PATH_MAX];
        if (snprintf(tmp, sizeof tmp, "%s.nemo-new", to) >= (int)sizeof tmp) return FOPE_FAIL;
        if (exists(tmp)) remove_tree(j, tmp, 0);
        rc = place(j, src, tmp, buf, cap);
        if (rc != FOPE_OK) return rc;
        if (remove_tree(j, to, 0) != 0 || rename(tmp, to) != 0) return FOPE_FAIL;   // the new one stays as .nemo-new
    }
    if (rc == FOPE_OK && j->kind == FOP_MOVE && exists(src)) rc = remove_tree(j, src, 0) == 0 ? FOPE_OK : FOPE_FAIL;
    return rc;
}

int fileop_run(FileJob *j, char *buf, size_t cap) {
    int rc = FOPE_OK;
    for (int i = 0; i < j->n_src; i++) {
        if (j->cancel) return FOPE_CANCELLED;
        int r = run_one(j, i, buf, cap);
        if (r == FOPE_CANCELLED) return r;
        if (r != FOPE_OK) { j->failed++; rc = FOPE_FAIL; }
    }
    return rc;
}
