#include "bgsave.h"
#include "safefile.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

static SDL_mutex *slot_lock, *write_lock;
static SDL_sem *wake;
static char *pending, pending_path[128];
static unsigned latest_gen, pending_gen;

// Writes text unless a newer request exists already (the generation check is under write_lock).
static int write_if_latest(const char *path, const char *text, unsigned gen) {
    SDL_LockMutex(write_lock);
    int rc = gen == latest_gen ? safe_write_text(path, text) : 0;
    SDL_UnlockMutex(write_lock);
    return rc;
}

static int worker(void *arg) {
    (void)arg;
    for (;;) {
        SDL_SemWait(wake);
        SDL_LockMutex(slot_lock);
        char *text = pending, path[sizeof pending_path];
        unsigned gen = pending_gen;
        memcpy(path, pending_path, sizeof path);
        pending = NULL;
        SDL_UnlockMutex(slot_lock);
        if (!text) continue;
        write_if_latest(path, text, gen);
        free(text);
    }
    return 0;
}

void bgsave_init(void) {
    slot_lock = SDL_CreateMutex();
    write_lock = SDL_CreateMutex();
    wake = SDL_CreateSemaphore(0);
    SDL_CreateThread(worker, "nemo_bgsave", NULL);
}

void bgsave_queue(const char *path, char *text) {
    SDL_LockMutex(slot_lock);
    free(pending);
    pending = text;
    strncpy(pending_path, path, sizeof pending_path - 1);
    pending_gen = ++latest_gen;
    SDL_UnlockMutex(slot_lock);
    SDL_SemPost(wake);
}

void bgsave_cancel(void) {
    SDL_LockMutex(slot_lock);
    free(pending);
    pending = NULL;
    ++latest_gen;
    SDL_UnlockMutex(slot_lock);
    SDL_LockMutex(write_lock);                   // wait for a write already in progress
    SDL_UnlockMutex(write_lock);
}

int bgsave_now(const char *path, char *text) {
    SDL_LockMutex(slot_lock);
    free(pending);
    pending = NULL;
    unsigned gen = ++latest_gen;
    SDL_UnlockMutex(slot_lock);
    int rc = write_if_latest(path, text, gen);
    free(text);
    return rc;
}
