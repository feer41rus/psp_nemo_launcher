#ifndef NEMO_FILEOPS_H
#define NEMO_FILEOPS_H

#include "fsbrowse.h"

// File manager operations (1.3.2): copy, move, delete of files and whole folders, with progress and
// cancel. fileop_run is meant for a worker thread; the UI reads the counters and sets cancel.
#define FOP_MAX_SRC 64

typedef enum { FOP_COPY, FOP_MOVE, FOP_DELETE } FopKind;
typedef enum { FC_REPLACE, FC_SKIP } FopConflict;
enum { FOPE_OK = 0, FOPE_FAIL = -1, FOPE_CANCELLED = -2, FOPE_INTO_SELF = -3 };

typedef struct FileJob {
    FopKind kind;
    char src[FOP_MAX_SRC][FS_PATH_MAX];
    int n_src;
    char dest[FS_PATH_MAX];            // the folder pasted into (not used by delete)
    FopConflict on_conflict;           // a name already in dest
    volatile int cancel;
    volatile int files_total, files_done;
    volatile long long bytes_total, bytes_done;
    int failed;                        // items that could not be done (the rest went on)
    void (*on_chunk)(struct FileJob *j);   // tests: called after every chunk written
} FileJob;

extern int fileops_test_no_rename;     // tests: rename() always fails, as across folders on some cards
extern const char *fileops_test_skip_name;   // tests: copying silently skips this name (a short readdir)

int fileop_plan(FileJob *j);           // counts files and bytes; FOPE_INTO_SELF for a folder into itself
int fileop_conflicts(const FileJob *j, char *first, size_t cap);   // top-level names already in dest
long long fileop_space_needed(const FileJob *j);   // bytes to be written (a move on one device: 0)
int fileop_run(FileJob *j, char *buf, size_t cap); // FOPE_OK, FOPE_CANCELLED, FOPE_FAIL (some failed)
int fileop_dir_stats(const char *dir, int *files, long long *bytes);   // 0 ok

#endif
