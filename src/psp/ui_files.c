// File manager (1.3.2): a window of the Organizer. Folders of the card (and the internal storage of a
// PSP Go), copy / move / delete / rename / new folder through a clipboard, and a text editor for
// configs with search. Listing and the operations run on threads below the music.
#include "ui.h"
#include "art.h"
#include "fileops.h"
#include "fsbrowse.h"
#include "gamesize.h"
#include "gfx.h"
#include "log.h"
#include "memory_hw.h"
#include "photo_hw.h"
#include "photofile.h"
#include "photos.h"
#include "platform.h"
#include "sjis.h"
#include "textdoc.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspkernel.h>
#include <sys/stat.h>
#include <time.h>

#define ROW_H 18
#define LIST_Y (TOP_BAR_H + 4)
#define LIST_ROWS 12
#define ED_ROW_H 16
#define ED_ROWS 14
#define COPY_BUF (256 * 1024)
#define FOPE_NO_SPACE -10

typedef enum { FV_LIST, FV_EDITOR } FilesView;
typedef enum { FM_NONE, FM_MENU, FM_QUESTION, FM_CONFLICT, FM_OSK } FilesModal;
typedef enum {
    MI_PASTE, MI_CLEAR, MI_COPY, MI_MOVE, MI_DELETE, MI_RENAME, MI_NEWDIR, MI_EDIT,   // list
    MI_SAVE, MI_FIND, MI_FIND_NEXT, MI_ADD, MI_DEL_LINE, MI_QUIT                      // editor
} FmItem;
// What "Yes" does.
typedef enum { Q_DELETE, Q_PROTECT_DELETE, Q_PROTECT_MOVE, Q_PROTECT_RENAME, Q_PROTECT_PASTE, Q_SAVE, Q_QUIT } Question;

typedef struct FilesWin {
    FilesView view;
    FilesModal modal;
    // where
    const char *devs[2];
    int n_dev, dev;
    char cwd[FS_PATH_MAX];
    char want[256];             // select this name when the listing arrives (after going up)
    FsList list;
    int sel, top, listing;
    uint32_t list_t0;
    // menu and questions
    FmItem items[10];
    int n_items, menu_sel;
    Question q;
    char q_text[400];
    int yes;                    // 1 = Yes selected (questions), conflict: 0 replace, 1 skip, 2 cancel
    // clipboard: paths copied / cut, pasted in another folder
    FopKind clip_kind;
    char (*clip)[FS_PATH_MAX];
    int n_clip;
    // operation on the worker thread
    FileJob *job;
    int changed_games;          // ISO/ or PSP/GAME changed: the library is rescanned on closing
    int viewing_photo;          // 1 a picture is open in the Gallery's viewer over this window, 2 a video
    // editor
    char ed_path[FS_PATH_MAX];
    TextDoc doc;
    int ed_sel, ed_top;
    char find[OSK_TEXT_MAX];
} FilesWin;

// ---------- threads ----------

static volatile int list_state;     // 0 idle, 1 running, 2 done
static FsList list_out;
static int list_rc;
static char list_dir[FS_PATH_MAX];

static int list_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    list_rc = fs_list(list_dir, &list_out);
    list_state = 2;
    sceKernelExitDeleteThread(0);
    return 0;
}

static volatile int job_state;      // 0 idle, 1 running, 2 done
static FileJob *job_run;
static int job_rc;

static int job_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    FileJob *j = job_run;
    int rc = fileop_plan(j);
    if (rc == FOPE_OK && j->kind != FOP_DELETE) {
        long long need = fileop_space_needed(j);
        MemCard card;
        char dev[8];
        snprintf(dev, sizeof dev, "%.4s", j->dest);
        if (need > 0 && memory_card_read(dev, &card) == 0 && card.free < need + 64 * 1024) rc = FOPE_NO_SPACE;
    }
    if (rc == FOPE_OK) {
        char *buf = malloc(COPY_BUF);
        rc = buf ? fileop_run(j, buf, COPY_BUF) : FOPE_FAIL;
        free(buf);
    }
    job_rc = rc;
    job_state = 2;
    sceKernelExitDeleteThread(0);
    return 0;
}

// The size of a folder for the delete question, counted off the main thread.
static volatile int stats_state;    // 0 idle, 1 running, 2 done
static char stats_path[FS_PATH_MAX];
static int stats_files;
static long long stats_bytes;

static int stats_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    fileop_dir_stats(stats_path, &stats_files, &stats_bytes);
    stats_state = 2;
    sceKernelExitDeleteThread(0);
    return 0;
}

int files_worker_busy(void) { return job_state == 1 || list_state == 1 || stats_state == 1; }

// ---------- helpers ----------

static int is_go(void) { return platform_model() == 4; }

static const FsEntry *cur(const FilesWin *s) {
    return s->sel >= 0 && s->sel < s->list.count ? &s->list.items[s->sel] : NULL;
}

static void path_of(const FilesWin *s, const FsEntry *e, char *out, size_t cap) { fs_join(out, cap, s->cwd, e->name); }

// Names come from the card in Shift-JIS (Cyrillic as 0x84 pairs): UTF-8 for the screen. Returns a
// rotating static buffer, so up to four can be used in one call.
static const char *shown(const char *raw) {
    static char buf[4][FS_PATH_MAX * 3 / 2];
    static int k;
    char *b = buf[k++ & 3];
    fs_display(raw, b, sizeof buf[0]);
    return b;
}

static int marked_count(const FilesWin *s) {
    int n = 0;
    for (int i = 0; i < s->list.count; i++) n += s->list.items[i].marked;
    return n;
}

// The marked entries, else the one under the cursor: their paths into out (up to FOP_MAX_SRC).
static int targets(const FilesWin *s, char (*out)[FS_PATH_MAX]) {
    int n = 0;
    for (int i = 0; i < s->list.count && n < FOP_MAX_SRC; i++)
        if (s->list.items[i].marked) path_of(s, &s->list.items[i], out[n++], FS_PATH_MAX);
    if (!n && cur(s)) path_of(s, cur(s), out[n++], FS_PATH_MAX);
    return n;
}

static int targets_protected(const FilesWin *s) {
    int marked = 0;
    char p[FS_PATH_MAX];
    for (int i = 0; i < s->list.count; i++)
        if (s->list.items[i].marked) {
            marked = 1;
            path_of(s, &s->list.items[i], p, sizeof p);
            if (fs_protected(p)) return 1;
        }
    if (marked || !cur(s)) return 0;
    path_of(s, cur(s), p, sizeof p);
    return fs_protected(p);
}

static void date_text(long mtime, char *out, size_t cap) {
    time_t t = (time_t)mtime;
    struct tm *tm = localtime(&t);
    if (tm) strftime(out, cap, "%d.%m.%Y %H:%M", tm);
    else out[0] = '\0';
}

static void size_text(long long b, char *out, size_t cap) {
    if (b < 1024) snprintf(out, cap, i18n_get() == LANG_EN ? "%lld B" : "%lld Б", b);
    else if (b < 1024 * 1024) snprintf(out, cap, i18n_get() == LANG_EN ? "%lld KB" : "%lld КБ", (b + 1023) / 1024);
    else game_size_text(out, cap, b, i18n_get() == LANG_EN);
}

// ---------- listing ----------

static void start_list(FilesWin *s) {
    fs_list_free(&s->list);
    s->sel = s->top = 0;
    s->listing = 1;
    if (list_state == 2) { fs_list_free(&list_out); list_state = 0; }
    if (list_state == 1) return;   // an older listing still runs: take_list asks again for this folder
    snprintf(list_dir, sizeof list_dir, "%s", s->cwd);
    s->list_t0 = platform_ms();
    list_state = 1;
    SceUID th = sceKernelCreateThread("nemo_fm_list", list_thread, 0x50, 0x10000, PSP_THREAD_ATTR_USER, NULL);
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
        list_rc = fs_list(list_dir, &list_out);
        list_state = 2;
    }
}

static void clamp(FilesWin *s) {
    if (s->sel >= s->list.count) s->sel = s->list.count - 1;
    if (s->sel < 0) s->sel = 0;
    if (s->sel < s->top) s->top = s->sel;
    if (s->sel >= s->top + LIST_ROWS) s->top = s->sel - LIST_ROWS + 1;
}

static void take_list(FilesWin *s) {
    if (strcmp(list_dir, s->cwd) != 0) {   // finished for a folder left meanwhile
        fs_list_free(&list_out);
        list_state = 0;
        start_list(s);
        return;
    }
    s->list = list_out;
    memset(&list_out, 0, sizeof list_out);
    list_state = 0;
    s->listing = 0;
    log_time("files_dir", platform_ms() - s->list_t0);
    log_msg("files: %s: %d entries%s rc=%d", s->cwd, s->list.count, s->list.truncated ? " (cut)" : "", list_rc);
    s->sel = 0;
    for (int i = 0; s->want[0] && i < s->list.count; i++)
        if (!strcmp(s->list.items[i].name, s->want)) s->sel = i;
    s->want[0] = '\0';
    s->top = 0;
    clamp(s);
}

static void go_to(FilesWin *s, const char *path, const char *select) {
    char p[FS_PATH_MAX], w[256];   // path / select may point into s (cwd, the list): copied first
    snprintf(p, sizeof p, "%s", path);
    snprintf(w, sizeof w, "%s", select ? select : "");
    memcpy(s->cwd, p, sizeof p);
    memcpy(s->want, w, sizeof w);
    start_list(s);
}

// ---------- operations ----------

static void start_job(App *a, FilesWin *s, FopKind kind, char (*src)[FS_PATH_MAX], int n, FopConflict on) {
    if (job_state != 0) return;
    if (!s->job) s->job = calloc(1, sizeof *s->job);
    if (!s->job) { ui_fail(a, S_FM_FAIL); return; }
    FileJob *j = s->job;
    memset(j, 0, sizeof *j);
    j->kind = kind;
    j->on_conflict = on;
    for (int i = 0; i < n; i++) {
        snprintf(j->src[i], FS_PATH_MAX, "%s", src[i]);
        if (fs_game_path(src[i])) s->changed_games = 1;
    }
    j->n_src = n;
    snprintf(j->dest, sizeof j->dest, "%s", s->cwd);
    if (kind != FOP_DELETE && fs_game_path(s->cwd)) s->changed_games = 1;
    log_msg("files: %s %d item(s) -> %s", kind == FOP_COPY ? "copy" : kind == FOP_MOVE ? "move" : "delete", n,
            kind == FOP_DELETE ? "-" : s->cwd);
    photo_thumbs_stop();   // the card is ours for a while
    job_run = j;
    job_state = 1;
    SceUID th = sceKernelCreateThread("nemo_fm_job", job_thread, 0x50, 0x20000, PSP_THREAD_ATTR_USER, NULL);
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
        job_state = 0;
        ui_fail(a, S_FM_FAIL);
    }
}

static uint32_t job_t0;

static void finish_job(App *a, FilesWin *s) {
    FileJob *j = s->job;
    int rc = job_rc;
    job_state = 0;
    const char *what = j->kind == FOP_COPY ? "files_copy" : j->kind == FOP_MOVE ? "files_move" : "files_delete";
    log_time(what, platform_ms() - job_t0);
    log_msg("files: %s: %d/%d files, %lld KB, failed %d, rc=%d", what, j->files_done, j->files_total,
            j->bytes_done / 1024, j->failed, rc);
    ui_toast(a, tr(rc == FOPE_OK ? S_FM_DONE : rc == FOPE_CANCELLED ? S_FM_CANCELLED
                   : rc == FOPE_INTO_SELF ? S_FM_INTO_SELF : rc == FOPE_NO_SPACE ? S_FM_NO_SPACE : S_FM_FAIL));
    if (j->kind == FOP_MOVE && rc == FOPE_OK) s->n_clip = 0;   // moved: nothing left to paste
    photo_tab_invalidate(a);
    platform_sync_card();
    go_to(s, s->cwd, cur(s) ? cur(s)->name : NULL);
}

static void paste(App *a, FilesWin *s, FopConflict on) {
    job_t0 = platform_ms();
    start_job(a, s, s->clip_kind, s->clip, s->n_clip, on);
}

static void ask(FilesWin *s, Question q, const char *text) {
    s->modal = FM_QUESTION;
    s->q = q;
    s->yes = 0;
    snprintf(s->q_text, sizeof s->q_text, "%s", text);
}

static void ask_delete(FilesWin *s) {
    int marked = marked_count(s);
    const FsEntry *e = cur(s);
    if (marked > 1) {
        snprintf(s->q_text, sizeof s->q_text, tr(S_FM_DEL_MANY_Q_FMT), marked);
    } else {
        if (marked == 1)
            for (int i = 0; i < s->list.count; i++)
                if (s->list.items[i].marked) e = &s->list.items[i];
        if (!e) return;
        if (e->is_dir) {   // the size arrives from a thread; the question can be answered meanwhile
            snprintf(s->q_text, sizeof s->q_text, tr(S_FM_DEL_DIR_COUNTING_FMT), shown(e->name));
            if (stats_state != 1) {
                path_of(s, e, stats_path, sizeof stats_path);
                stats_state = 1;
                SceUID th = sceKernelCreateThread("nemo_fm_stats", stats_thread, 0x50, 0x10000, PSP_THREAD_ATTR_USER, NULL);
                if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) stats_state = 0;
            }
        } else {
            snprintf(s->q_text, sizeof s->q_text, tr(S_FM_DEL_Q_FMT), shown(e->name));
        }
    }
    s->modal = FM_QUESTION;
    s->q = Q_DELETE;
    s->yes = 0;
}

static void open_osk(App *a, FilesWin *s, OskPurpose purpose, const char *initial) {
    a->osk_purpose = purpose;
    osk_init_mode(&a->osk, initial, OSK_MODE_FULL);
    if (purpose == OSK_FOR_FILE_RENAME || purpose == OSK_FOR_NEW_DIR) osk_set_length_rule(&a->osk, 1, OSK_TEXT_MAX - 1);
    if (purpose == OSK_FOR_FIND) osk_set_length_rule(&a->osk, 1, OSK_TEXT_MAX - 1);
    s->modal = FM_OSK;
}

static void do_rename(App *a, FilesWin *s, const char *typed) {
    const FsEntry *e = cur(s);
    if (!e || strchr(typed, '/')) return;
    if (utf8_len(typed) > 255) { ui_toast(a, tr(S_FM_NAME_LONG)); return; }
    char name[FS_PATH_MAX];   // as the card stores it
    utf8_to_sjis(typed, name, sizeof name);
    if (!strcmp(name, e->name)) return;
    char from[FS_PATH_MAX], to[FS_PATH_MAX];
    path_of(s, e, from, sizeof from);
    fs_join(to, sizeof to, s->cwd, name);
    struct stat st;
    if (stat(to, &st) == 0 && strcasecmp(e->name, name) != 0) { ui_toast(a, tr(S_FM_NAME_TAKEN)); return; }
    int rc = rename(from, to);
    log_msg("files: rename %s -> %s rc=%d", shown(from), typed, rc);
    if (rc != 0) { ui_fail(a, S_FM_FAIL); return; }
    if (fs_game_path(from)) s->changed_games = 1;
    photo_tab_invalidate(a);
    go_to(s, s->cwd, name);
}

static void do_newdir(App *a, FilesWin *s, const char *typed) {
    if (strchr(typed, '/')) { ui_fail(a, S_FM_FAIL); return; }
    if (utf8_len(typed) > 255) { ui_toast(a, tr(S_FM_NAME_LONG)); return; }
    char name[FS_PATH_MAX];   // as the card stores it
    utf8_to_sjis(typed, name, sizeof name);
    char p[FS_PATH_MAX];
    fs_join(p, sizeof p, s->cwd, name);
    struct stat st;
    if (stat(p, &st) == 0) { ui_toast(a, tr(S_FM_NAME_TAKEN)); return; }
    int rc = mkdir(p, 0777);
    log_msg("files: mkdir %s rc=%d", p, rc);
    if (rc != 0) { ui_fail(a, S_FM_FAIL); return; }
    if (fs_game_path(p)) s->changed_games = 1;
    go_to(s, s->cwd, name);
}

// ---------- editor ----------

static void ed_clamp(FilesWin *s) {
    if (s->ed_sel >= s->doc.count) s->ed_sel = s->doc.count - 1;
    if (s->ed_sel < 0) s->ed_sel = 0;
    if (s->ed_sel < s->ed_top) s->ed_top = s->ed_sel;
    if (s->ed_sel >= s->ed_top + ED_ROWS) s->ed_top = s->ed_sel - ED_ROWS + 1;
}

static void open_editor(App *a, FilesWin *s, const char *path) {
    struct stat st;
    char bak[FS_PATH_MAX + 16];   // a save cut short by a power loss: put the file back
    snprintf(bak, sizeof bak, "%s.nemo-bak", path);
    if (stat(path, &st) != 0 && stat(bak, &st) == 0) log_msg("files: restore %s rc=%d", path, rename(bak, path));
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) { ui_fail(a, S_FM_FAIL); return; }
    if (st.st_size > TEXTDOC_MAX_BYTES) { ui_toast(a, tr(S_ED_TOO_BIG)); return; }
    FILE *f = fopen(path, "rb");
    if (!f) { ui_fail(a, S_FM_FAIL); return; }
    size_t n = (size_t)st.st_size;
    char *buf = malloc(n + 1);
    size_t got = buf ? fread(buf, 1, n, f) : 0;
    fclose(f);
    int rc = buf && got == n ? textdoc_parse(&s->doc, buf, n) : -1;
    free(buf);
    if (rc != 0) { ui_toast(a, tr(S_ED_NOT_TEXT)); return; }
    snprintf(s->ed_path, sizeof s->ed_path, "%s", path);
    s->view = FV_EDITOR;
    s->ed_sel = s->ed_top = 0;
    log_msg("files: edit %s (%d lines, %s%s)", path, s->doc.count, s->doc.crlf ? "crlf" : "lf", s->doc.bom ? ", bom" : "");
}

static void close_editor(FilesWin *s) {
    textdoc_free(&s->doc);
    s->view = FV_LIST;
    s->modal = FM_NONE;
}

static int write_doc(void *ctx, const char *tmp) {
    const FilesWin *s = ctx;
    size_t n;
    char *text = textdoc_join(&s->doc, &n);
    if (!text) return -1;
    FILE *f = fopen(tmp, "wb");
    int ok = f && fwrite(text, 1, n, f) == n;
    if (f && fclose(f) != 0) ok = 0;
    free(text);
    return ok ? 0 : -1;
}

static void save_doc(App *a, FilesWin *s) {
    uint32_t t0 = platform_ms();
    // An empty file would fail the "written" check of photofile_replace: write it directly.
    int rc;
    if (s->doc.count == 1 && !s->doc.lines[0][0] && !s->doc.bom && !s->doc.final_nl) {
        FILE *f = fopen(s->ed_path, "wb");
        rc = f && fclose(f) == 0 ? 0 : -1;
    } else {
        rc = photofile_replace_sfx(s->ed_path, ".nemo-tmp", ".nemo-bak", write_doc, s);
    }
    log_time("files_save", platform_ms() - t0);
    log_msg("files: save %s rc=%d", s->ed_path, rc);
    if (rc == 0) {
        s->doc.dirty = 0;
        memset(s->doc.changed, 0, (size_t)s->doc.count);
        platform_sync_card();
    }
    ui_toast(a, tr(rc == 0 ? S_ED_SAVED : S_ED_SAVE_FAIL));
}

static void find_next(App *a, FilesWin *s, int from) {
    if (!s->find[0]) { open_osk(a, s, OSK_FOR_FIND, ""); return; }
    int i = textdoc_find(&s->doc, from, s->find);
    if (i < 0) { ui_toast(a, tr(S_ED_NOT_FOUND)); return; }
    s->ed_sel = i;
    ed_clamp(s);
}

// ---------- menu ----------

static StrId item_label(FmItem it) {
    static const StrId L[] = { S_FM_PASTE, S_FM_CLEAR, S_FM_COPY, S_FM_MOVE, S_FM_DELETE, S_FM_RENAME, S_FM_NEWDIR,
                               S_FM_EDIT, S_ED_SAVE, S_ED_FIND, S_ED_FIND_NEXT, S_ED_ADD, S_ED_DEL, S_ED_QUIT };
    return L[it];
}

static void open_menu(FilesWin *s) {
    s->n_items = 0;
    if (s->view == FV_EDITOR) {
        static const FmItem E[] = { MI_SAVE, MI_FIND, MI_FIND_NEXT, MI_ADD, MI_DEL_LINE, MI_QUIT };
        for (size_t i = 0; i < sizeof E / sizeof E[0]; i++) s->items[s->n_items++] = E[i];
    } else {
        const FsEntry *e = cur(s);
        if (s->n_clip) { s->items[s->n_items++] = MI_PASTE; s->items[s->n_items++] = MI_CLEAR; }
        if (e) {
            s->items[s->n_items++] = MI_COPY;
            s->items[s->n_items++] = MI_MOVE;
            s->items[s->n_items++] = MI_DELETE;
            if (marked_count(s) == 0) s->items[s->n_items++] = MI_RENAME;
        }
        s->items[s->n_items++] = MI_NEWDIR;
        if (e && !e->is_dir && marked_count(s) == 0 && fs_is_textfile(e->name)) s->items[s->n_items++] = MI_EDIT;
    }
    s->menu_sel = 0;
    s->modal = FM_MENU;
}

static void to_clipboard(App *a, FilesWin *s, FopKind kind) {
    if (!s->clip) s->clip = calloc(FOP_MAX_SRC, sizeof *s->clip);
    if (!s->clip) return;
    s->n_clip = targets(s, s->clip);
    s->clip_kind = kind;
    for (int i = 0; i < s->list.count; i++) s->list.items[i].marked = 0;
    char m[160];
    snprintf(m, sizeof m, tr(kind == FOP_COPY ? S_FM_CLIP_COPY_FMT : S_FM_CLIP_MOVE_FMT), s->n_clip);
    ui_toast(a, m);
}

// Paste: names already in this folder are asked about first (replace / skip / cancel).
static void start_paste(App *a, FilesWin *s) {
    if (!s->job) s->job = calloc(1, sizeof *s->job);
    if (!s->job || !s->n_clip) return;
    FileJob *j = s->job;
    memset(j, 0, sizeof *j);
    j->kind = s->clip_kind;
    for (int i = 0; i < s->n_clip; i++) snprintf(j->src[i], FS_PATH_MAX, "%s", s->clip[i]);
    j->n_src = s->n_clip;
    snprintf(j->dest, sizeof j->dest, "%s", s->cwd);
    char first[256];
    int n = fileop_conflicts(j, first, sizeof first);
    if (n > 0) {
        if (n > 1) snprintf(s->q_text, sizeof s->q_text, tr(S_FM_CONFLICT_MORE_FMT), shown(first), n - 1);
        else snprintf(s->q_text, sizeof s->q_text, tr(S_FM_CONFLICT_Q_FMT), shown(first));
        s->modal = FM_CONFLICT;
        s->yes = 0;
        return;
    }
    paste(a, s, FC_REPLACE);
}

// Replacing in ARK's or Nemo's folders is asked about first, like deleting there.
static int paste_hits_protected(const FilesWin *s) {
    char to[FS_PATH_MAX];
    struct stat st;
    for (int i = 0; i < s->n_clip; i++)
        if (fs_join(to, sizeof to, s->cwd, fs_name(s->clip[i])) == 0 && fs_protected(to) && stat(to, &st) == 0) return 1;
    return 0;
}

static void menu_pick(App *a, FilesWin *s, FmItem it) {
    s->modal = FM_NONE;
    if ((it == MI_COPY || it == MI_MOVE || it == MI_DELETE) && marked_count(s) > FOP_MAX_SRC) {
        char m[96];
        snprintf(m, sizeof m, tr(S_FM_TOO_MANY_FMT), FOP_MAX_SRC);
        ui_toast(a, m);
        return;
    }
    switch (it) {
    case MI_PASTE:
        if (paste_hits_protected(s)) ask(s, Q_PROTECT_PASTE, tr(S_FM_PROTECT_Q));
        else start_paste(a, s);
        break;
    case MI_CLEAR: s->n_clip = 0; break;
    case MI_COPY: to_clipboard(a, s, FOP_COPY); break;
    case MI_MOVE:
        if (targets_protected(s)) ask(s, Q_PROTECT_MOVE, tr(S_FM_PROTECT_Q));
        else to_clipboard(a, s, FOP_MOVE);
        break;
    case MI_DELETE:
        if (targets_protected(s)) ask(s, Q_PROTECT_DELETE, tr(S_FM_PROTECT_Q));
        else ask_delete(s);
        break;
    case MI_RENAME: {
        char p[FS_PATH_MAX];
        if (!cur(s)) break;
        path_of(s, cur(s), p, sizeof p);
        if (fs_protected(p)) ask(s, Q_PROTECT_RENAME, tr(S_FM_PROTECT_Q));
        else open_osk(a, s, OSK_FOR_FILE_RENAME, shown(cur(s)->name));
        break;
    }
    case MI_NEWDIR: open_osk(a, s, OSK_FOR_NEW_DIR, ""); break;
    case MI_EDIT: {
        char p[FS_PATH_MAX];
        if (cur(s)) { path_of(s, cur(s), p, sizeof p); open_editor(a, s, p); }
        break;
    }
    case MI_SAVE: {
        char q[400];
        snprintf(q, sizeof q, tr(S_ED_SAVE_Q_FMT), shown(fs_name(s->ed_path)));
        ask(s, Q_SAVE, q);
        break;
    }
    case MI_FIND: open_osk(a, s, OSK_FOR_FIND, s->find); break;
    case MI_FIND_NEXT: find_next(a, s, s->ed_sel + 1); break;
    case MI_ADD:
        if (textdoc_insert(&s->doc, s->ed_sel, "") == 0) { s->ed_sel++; ed_clamp(s); }
        break;
    case MI_DEL_LINE: textdoc_delete(&s->doc, s->ed_sel); ed_clamp(s); break;
    case MI_QUIT: close_editor(s); break;
    }
}

static void question_yes(App *a, FilesWin *s) {
    s->modal = FM_NONE;
    switch (s->q) {
    case Q_PROTECT_DELETE: ask_delete(s); break;
    case Q_PROTECT_MOVE: to_clipboard(a, s, FOP_MOVE); break;
    case Q_PROTECT_RENAME: if (cur(s)) open_osk(a, s, OSK_FOR_FILE_RENAME, shown(cur(s)->name)); break;
    case Q_PROTECT_PASTE: start_paste(a, s); break;
    case Q_DELETE: {
        char (*paths)[FS_PATH_MAX] = malloc(FOP_MAX_SRC * FS_PATH_MAX);   // only for the moment
        if (!paths) break;
        int n = targets(s, paths);
        job_t0 = platform_ms();
        start_job(a, s, FOP_DELETE, paths, n, FC_REPLACE);
        free(paths);
        break;
    }
    case Q_SAVE: save_doc(a, s); break;
    case Q_QUIT: close_editor(s); break;
    }
}

static void osk_done(App *a, FilesWin *s) {
    s->modal = FM_NONE;
    const char *text = a->osk.text;
    switch (a->osk_purpose) {
    case OSK_FOR_FILE_RENAME: do_rename(a, s, text); break;
    case OSK_FOR_NEW_DIR: do_newdir(a, s, text); break;
    case OSK_FOR_EDIT_LINE:
        if (strcmp(text, s->doc.lines[s->ed_sel]) != 0) textdoc_set(&s->doc, s->ed_sel, text);
        break;
    case OSK_FOR_FIND:
        snprintf(s->find, sizeof s->find, "%s", text);
        find_next(a, s, s->ed_sel);
        break;
    default: break;
    }
}

// ---------- the window ----------

static FilesWin *win(const App *a) { return a->files; }

int files_is_open(const App *a) { return a->files != NULL; }

int files_busy(const App *a) {
    const FilesWin *s = win(a);
    return s && (job_state == 1 || s->modal == FM_OSK || (s->view == FV_EDITOR && s->doc.dirty));
}

int files_osk_open(const App *a) { return a->files && a->files->modal == FM_OSK; }

const char *files_screen(const App *a) {
    const FilesWin *s = win(a);
    if (!s) return "";
    if (job_state == 1) return "progress";
    if (s->modal == FM_OSK) return "keyboard";
    return s->view == FV_EDITOR ? "editor" : "list";
}

void files_open(App *a) {
    if (a->files) return;
    art_reset();   // the RAM goes to the listing and the copy buffer; the music keeps playing
    FilesWin *s = calloc(1, sizeof *s);
    if (!s) return;
    a->files = s;
    if (is_go()) { s->devs[0] = "ef0:/"; s->devs[1] = "ms0:/"; s->n_dev = 2; }
    else { s->devs[0] = "ms0:/"; s->n_dev = 1; }
    log_msg("files: window open");
    go_to(s, s->devs[0], NULL);
}

static void close_window(App *a) {
    FilesWin *s = a->files;
    if (!s) return;
    if (list_state == 2) { fs_list_free(&list_out); list_state = 0; }   // a running one is dropped when it ends
    textdoc_free(&s->doc);
    fs_list_free(&s->list);
    int rescan = s->changed_games;
    free(s->clip);
    free(s->job);
    free(s);
    a->files = NULL;
    platform_log_memory("files-closed");
    if (rescan) library_rescan(a);
}

void files_worker_stop(void) {
    if (job_state != 1) return;
    if (job_run) job_run->cancel = 1;
    for (int i = 0; i < 500 && job_state == 1; i++) sceKernelDelayThread(10000);
}

static void list_action(App *a, FilesWin *s, Action act) {
    switch (act) {
    case ACT_UP: if (s->list.count) { s->sel = (s->sel + s->list.count - 1) % s->list.count; clamp(s); } break;
    case ACT_DOWN: if (s->list.count) { s->sel = (s->sel + 1) % s->list.count; clamp(s); } break;
    case ACT_TAB_PREV: s->sel -= LIST_ROWS; clamp(s); break;
    case ACT_TAB_NEXT: s->sel += LIST_ROWS; clamp(s); break;
    case ACT_LEFT:
    case ACT_RIGHT:
        if (s->n_dev > 1) {
            s->dev = (s->dev + 1) % s->n_dev;
            go_to(s, s->devs[s->dev], NULL);
        }
        break;
    case ACT_QUICK:
        if (cur(s)) { s->list.items[s->sel].marked ^= 1; if (s->sel + 1 < s->list.count) { s->sel++; clamp(s); } }
        break;
    case ACT_MENU: open_menu(s); break;
    case ACT_CONFIRM: {
        const FsEntry *e = cur(s);
        if (!e) break;
        char p[FS_PATH_MAX];
        path_of(s, e, p, sizeof p);
        if (e->is_dir) go_to(s, p, NULL);
        else if (fs_is_textfile(e->name)) open_editor(a, s, p);
        else if (photo_is_image(e->name)) { s->viewing_photo = 1; photo_open_file(a, s->cwd, e->name); }
        else if (video_is_file(e->name)) { s->viewing_photo = 2; video_open_file(a, p); }
        else open_menu(s);
        break;
    }
    case ACT_BACK: {
        if (fs_is_root(s->cwd)) { close_window(a); break; }
        char name[256];
        snprintf(name, sizeof name, "%s", fs_name(s->cwd));
        char up[FS_PATH_MAX];
        snprintf(up, sizeof up, "%s", s->cwd);
        fs_parent(up);
        go_to(s, up, name);
        break;
    }
    default: break;
    }
}

static void editor_action(App *a, FilesWin *s, Action act) {
    switch (act) {
    case ACT_UP: if (s->ed_sel > 0) s->ed_sel--; ed_clamp(s); break;
    case ACT_DOWN: if (s->ed_sel + 1 < s->doc.count) s->ed_sel++; ed_clamp(s); break;
    case ACT_TAB_PREV: s->ed_sel -= ED_ROWS; ed_clamp(s); break;
    case ACT_TAB_NEXT: s->ed_sel += ED_ROWS; ed_clamp(s); break;
    case ACT_QUICK:   // a line longer than the keyboard holds would be cut on saving: not edited
        if (strlen(s->doc.lines[s->ed_sel]) >= OSK_TEXT_MAX - 1) ui_toast(a, tr(S_ED_LINE_LONG));
        else open_osk(a, s, OSK_FOR_EDIT_LINE, s->doc.lines[s->ed_sel]);
        break;
    case ACT_START: find_next(a, s, s->ed_sel + 1); break;
    case ACT_MENU: open_menu(s); break;
    case ACT_BACK:
    case ACT_HOME:
        if (s->doc.dirty) ask(s, Q_QUIT, tr(S_ED_QUIT_Q));
        else close_editor(s);
        break;
    default: break;
    }
}

void files_action(App *a, Action act) {
    FilesWin *s = win(a);
    if (!s) return;
    if (job_state == 1) {   // the progress screen: only ○ cancels
        if (act == ACT_BACK && s->job) s->job->cancel = 1;
        return;
    }
    if (s->listing) { if (act == ACT_BACK) close_window(a); return; }
    switch (s->modal) {
    case FM_OSK: {
        OskState st = osk_handle(&a->osk, act);
        if (st == OSK_DONE) osk_done(a, s);
        else if (st == OSK_CANCEL) s->modal = FM_NONE;
        return;
    }
    case FM_MENU:
        if (act == ACT_UP) s->menu_sel = (s->menu_sel + s->n_items - 1) % s->n_items;
        else if (act == ACT_DOWN) s->menu_sel = (s->menu_sel + 1) % s->n_items;
        else if (act == ACT_CONFIRM) menu_pick(a, s, s->items[s->menu_sel]);
        else if (act == ACT_BACK || act == ACT_MENU || act == ACT_HOME) s->modal = FM_NONE;
        return;
    case FM_QUESTION:
        if (act == ACT_LEFT || act == ACT_RIGHT) s->yes = !s->yes;
        else if (act == ACT_CONFIRM) { if (s->yes) question_yes(a, s); else s->modal = FM_NONE; }
        else if (act == ACT_BACK || act == ACT_HOME) s->modal = FM_NONE;
        return;
    case FM_CONFLICT:
        if (act == ACT_LEFT) s->yes = (s->yes + 2) % 3;
        else if (act == ACT_RIGHT) s->yes = (s->yes + 1) % 3;
        else if (act == ACT_CONFIRM) {
            s->modal = FM_NONE;
            if (s->yes < 2) paste(a, s, s->yes == 0 ? FC_REPLACE : FC_SKIP);
        } else if (act == ACT_BACK || act == ACT_HOME) s->modal = FM_NONE;
        return;
    case FM_NONE: break;
    }
    if (s->view == FV_EDITOR) editor_action(a, s, act);
    else if (act != ACT_HOME) list_action(a, s, act);
}

void files_tick(App *a) {
    FilesWin *s = win(a);
    if (!s) return;
    if (job_state == 2) finish_job(a, s);
    if (list_state == 2 && s->listing) take_list(s);
    if (s->viewing_photo == 1 && !photo_tab_owns_buttons(a)) {   // back from the viewer: rotated / deleted files show
        s->viewing_photo = 0;
        go_to(s, s->cwd, fs_name(photo_last_path(a)));
    }
    if (s->viewing_photo == 2 && !video_win_is_open(a)) {   // back from a video: the cursor stays on it
        s->viewing_photo = 0;
        clamp(s);
    }
    if (stats_state == 2) {
        stats_state = 0;
        char counting[400];   // still the question this count was started for?
        snprintf(counting, sizeof counting, tr(S_FM_DEL_DIR_COUNTING_FMT), shown(fs_name(stats_path)));
        if (s->modal == FM_QUESTION && s->q == Q_DELETE && !strcmp(s->q_text, counting)) {
            char sz[32];
            size_text(stats_bytes, sz, sizeof sz);
            snprintf(s->q_text, sizeof s->q_text, tr(S_FM_DEL_DIR_Q_FMT), shown(fs_name(stats_path)), stats_files, sz);
        }
    }
    if (a->nav.tab != TAB_ORGANIZER && !files_busy(a)) close_window(a);
}

// ---------- drawing ----------

static void draw_box(int w, int h, int *x, int *y) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000a0);
    *x = (SCREEN_W - w) / 2;
    *y = (SCREEN_H - h) / 2;
    gfx_round_rect(*x, *y, w, h, 12, 0x1a2230ff);
    gfx_round_rect_outline(*x, *y, w, h, 12, 0xffffff20);
}

// Splits text into at most three lines that fit w (at spaces).
static int wrap(const char *text, int w, char lines[3][200]) {
    int n = 0;
    const char *p = text;
    while (*p && n < 3) {
        size_t best = 0, i = 0;
        char tmp[200];
        while (p[i] && i < sizeof tmp - 1) {
            if (p[i] == ' ' || !p[i + 1]) {
                size_t len = p[i] == ' ' ? i : i + 1;
                memcpy(tmp, p, len);
                tmp[len] = '\0';
                if (gfx_text_width(FONT_SMALL, tmp) > w && best) break;
                best = len;
            }
            i++;
        }
        if (!best) best = strlen(p) < sizeof tmp - 1 ? strlen(p) : sizeof tmp - 1;
        memcpy(lines[n], p, best);
        lines[n][best] = '\0';
        n++;
        p += best;
        while (*p == ' ') p++;
    }
    return n;
}

static void draw_buttons(int x, int y, int w, const StrId *labels, int n, int sel, int danger) {
    int bw = n == 3 ? 104 : 104, gap = 12, total = n * bw + (n - 1) * gap, bx = x + (w - total) / 2;
    for (int i = 0; i < n; i++, bx += bw + gap) {
        int on = i == sel;
        gfx_round_rect(bx, y, bw, 24, 12, on ? (danger && i == 0 ? 0xd9534fff : 0x2b5fa8ff) : COL_PANEL);
        const char *t = tr(labels[i]);
        gfx_text_fit(FONT_BOLD, bx + (bw - gfx_text_width(FONT_BOLD, t)) / 2, y + 4, bw - 8, COL_TEXT, t);
    }
}

static void draw_question(const FilesWin *s) {
    char lines[3][200];
    int w = 400, n = wrap(s->q_text, w - 32, lines), h = 64 + n * 17, x, y;
    draw_box(w, h, &x, &y);
    for (int i = 0; i < n; i++) gfx_text(FONT_SMALL, x + 16, y + 14 + i * 17, COL_TEXT, lines[i]);
    if (s->modal == FM_CONFLICT) {
        static const StrId L[3] = { S_FM_REPLACE, S_FM_SKIP, S_FM_CANCEL };
        draw_buttons(x, y + h - 36, w, L, 3, s->yes, 0);
    } else {
        static const StrId L[2] = { S_YES, S_NO };
        int danger = s->q == Q_DELETE || s->q == Q_PROTECT_DELETE || s->q == Q_QUIT;
        draw_buttons(x, y + h - 36, w, L, 2, s->yes ? 0 : 1, danger);
    }
}

static void draw_menu(const FilesWin *s) {
    int w = 240, h = 16 + s->n_items * 24, x, y;
    draw_box(w, h, &x, &y);
    for (int i = 0; i < s->n_items; i++) {
        int ry = y + 8 + i * 24;
        if (i == s->menu_sel) gfx_round_rect(x + 6, ry, w - 12, 22, 6, 0x24324aff);
        gfx_text(FONT_BODY, x + 18, ry + 2, COL_TEXT, tr(item_label(s->items[i])));
    }
}

static void draw_header(const char *left, const char *right, uint32_t right_col) {
    gfx_rect(0, 0, SCREEN_W, TOP_BAR_H, COL_BAR);
    int rw = right && right[0] ? gfx_text_width(FONT_SMALL, right) : 0;
    gfx_text_fit(FONT_BOLD, 10, 3, SCREEN_W - 30 - rw, COL_TEXT, left);
    if (rw) gfx_text(FONT_SMALL, SCREEN_W - 10 - rw, 4, right_col, right);
}

static void draw_progress(const App *a, const FilesWin *s) {
    const FileJob *j = s->job;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    draw_header(tr(S_ORG_FILES), "", COL_MUTED);
    char line[160], sz1[32], sz2[32];
    StrId fmt = j->kind == FOP_COPY ? S_FM_COPYING_FMT : j->kind == FOP_MOVE ? S_FM_MOVING_FMT : S_FM_DELETING_FMT;
    int done = j->files_done < j->files_total ? j->files_done + 1 : j->files_done;
    snprintf(line, sizeof line, tr(fmt), done, j->files_total);
    gfx_text_fit(FONT_BODY, 40, 100, SCREEN_W - 80, COL_TEXT, line);
    long long total = j->kind == FOP_DELETE ? j->files_total : j->bytes_total;
    long long now = j->kind == FOP_DELETE ? j->files_done : j->bytes_done;
    int pct = total > 0 ? (int)(now * 100 / total) : 0;
    if (pct > 100) pct = 100;
    gfx_round_rect(40, 128, SCREEN_W - 80, 10, 5, COL_PANEL);
    if (pct) gfx_round_rect(40, 128, (SCREEN_W - 80) * pct / 100, 10, 5, COL_ACCENT);
    if (j->kind != FOP_DELETE) {
        size_text(j->bytes_done, sz1, sizeof sz1);
        size_text(j->bytes_total, sz2, sizeof sz2);
        snprintf(line, sizeof line, "%s / %s", sz1, sz2);
        gfx_text(FONT_SMALL, 40, 146, COL_MUTED, line);
    }
    (void)a;
}

static void draw_list(const App *a, const FilesWin *s) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    char right[48] = "";
    int marked = marked_count(s);
    if (marked) snprintf(right, sizeof right, tr(S_FM_MARKED_FMT), marked);
    else if (s->n_dev > 1) snprintf(right, sizeof right, "%s", tr(strncmp(s->cwd, "ef0", 3) ? S_FM_CARD : S_FM_INTERNAL));
    draw_header(shown(s->cwd), right, marked ? COL_ACCENT : COL_MUTED);
    int y0 = LIST_Y;
    if (s->n_clip) {   // what waits to be pasted
        char c[160];
        snprintf(c, sizeof c, tr(s->clip_kind == FOP_COPY ? S_FM_CLIP_COPY_FMT : S_FM_CLIP_MOVE_FMT), s->n_clip);
        gfx_text_fit(FONT_SMALL, 12, y0, SCREEN_W - 24, COL_WARN, c);
        y0 += 16;
    }
    if (s->listing || !s->list.count) {
        const char *m = tr(s->listing ? S_FM_LOADING : S_FM_EMPTY);
        gfx_text(FONT_SMALL, (SCREEN_W - gfx_text_width(FONT_SMALL, m)) / 2, SCREEN_H / 2 - 8, COL_MUTED, m);
        return;
    }
    int rows = s->n_clip ? LIST_ROWS - 1 : LIST_ROWS;
    for (int r = 0; r < rows; r++) {
        int i = s->top + r;
        if (i >= s->list.count) break;
        const FsEntry *e = &s->list.items[i];
        int y = y0 + r * ROW_H;
        if (i == s->sel) gfx_round_rect(6, y, SCREEN_W - 12, ROW_H - 1, 5, 0x24324aff);
        if (e->marked) gfx_rect(6, y + 2, 3, ROW_H - 5, COL_ACCENT);
        if (e->is_dir) gfx_round_rect(14, y + 4, 14, 10, 2, 0xe0b04cff);   // folder
        else gfx_round_rect(16, y + 3, 10, 12, 2, fs_is_textfile(e->name) ? 0x8fb8e8ff : 0x7d8796ff);
        char meta[64] = "", sz[32], dt[32];
        date_text(e->mtime, dt, sizeof dt);
        if (e->is_dir) snprintf(meta, sizeof meta, "%s", dt);
        else { size_text(e->size, sz, sizeof sz); snprintf(meta, sizeof meta, "%s   %s", sz, dt); }
        int mw = gfx_text_width(FONT_SMALL, meta);
        gfx_text_fit(FONT_SMALL, 36, y + 1, SCREEN_W - 60 - mw, COL_TEXT, shown(e->name));
        gfx_text(FONT_SMALL, SCREEN_W - 14 - mw, y + 1, COL_MUTED, meta);
    }
    if (s->list.truncated) {
        char t[64];
        snprintf(t, sizeof t, tr(S_FM_TRUNC_FMT), FS_LIST_MAX);
        gfx_text(FONT_SMALL, SCREEN_W - 12 - gfx_text_width(FONT_SMALL, t), SCREEN_H - HINT_BAR_H - 16, COL_WARN, t);
    }
    (void)a;
}

static void draw_editor(const FilesWin *s) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    draw_header(shown(fs_name(s->ed_path)), s->doc.dirty ? tr(S_ED_UNSAVED) : "", COL_WARN);
    int y0 = TOP_BAR_H + 4;
    for (int r = 0; r < ED_ROWS; r++) {
        int i = s->ed_top + r;
        if (i >= s->doc.count) break;
        int y = y0 + r * ED_ROW_H;
        if (i == s->ed_sel) gfx_round_rect(4, y, SCREEN_W - 8, ED_ROW_H - 1, 4, 0x24324aff);
        if (s->doc.changed[i]) gfx_rect(4, y + 2, 3, ED_ROW_H - 5, COL_WARN);
        char num[12];
        snprintf(num, sizeof num, "%d", i + 1);
        gfx_text(FONT_SMALL, 40 - gfx_text_width(FONT_SMALL, num), y, COL_MUTED, num);
        gfx_text_fit(FONT_SMALL, 48, y, SCREEN_W - 56, COL_TEXT, s->doc.lines[i]);
    }
}

void files_draw(const App *a) {
    const FilesWin *s = win(a);
    if (!s) return;
    if (job_state == 1 && s->job) { draw_progress(a, s); return; }
    if (s->view == FV_EDITOR) draw_editor(s);
    else draw_list(a, s);
    switch (s->modal) {
    case FM_MENU: draw_menu(s); break;
    case FM_QUESTION: case FM_CONFLICT: draw_question(s); break;
    case FM_OSK: ui_osk_draw(a); break;
    case FM_NONE: break;
    }
}

void files_hints(const App *a) {
    const FilesWin *s = win(a);
    if (!s) return;
    if (job_state == 1) {
        static const Hint h[] = { { ACT_BACK, S_HINT_CANCEL } };
        ui_hints_draw(a, h, 1);
    } else if (s->modal == FM_OSK) {   // the same as the keyboard everywhere else
        static const Hint h[] = { { ACT_CONFIRM, S_OSK_HINT_TYPE }, { ACT_QUICK, S_OSK_HINT_DEL },
                                  { ACT_TAB_PREV, S_OSK_HINT_CAPS }, { ACT_BACK, S_OSK_HINT_CANCEL },
                                  { ACT_START, S_OSK_HINT_DONE } };
        ui_hints_draw(a, h, 5);
    } else if (s->modal != FM_NONE) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    } else if (s->view == FV_EDITOR) {
        static const Hint h[] = { { ACT_QUICK, S_HINT_EDIT }, { ACT_START, S_HINT_FIND_NEXT },
                                  { ACT_MENU, S_HINT_MENU }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 4);
    } else {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_OPEN }, { ACT_QUICK, S_HINT_MARK },
                                  { ACT_MENU, S_HINT_MENU }, { ACT_BACK, S_HINT_UP } };
        ui_hints_draw(a, h, 4);
    }
}
