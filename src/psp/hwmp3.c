#include "hwmp3.h"
#include "log.h"
#include "mp3info.h"
#include "platform.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <pspiofilemgr.h>
#include <pspmp3.h>
#include <psputility.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define OUT_RATE 44100
#define RING_BYTES (128 * 1024)          // ~0.74 s of 44.1 kHz stereo S16
#define PREFILL_BYTES (32 * 1024)        // ~0.19 s decoded before the track starts
#define DECODE_ROOM (16 * 1024)          // decode only when this much ring space is free
#define DECODER_DELAY 529                // samples the MP3 decoder adds (LAME tag convention)
#define HEAD_BYTES 4096
#define NO_LIMIT UINT64_MAX

struct HwTrack {
    int used;
    SceUID fd;
    int handle;
    Mp3Info info;
    int channels, rate;
    SDL_AudioStream *conv;
    uint8_t *ring;
    volatile uint32_t wr, rd;            // byte counters, ring index = counter % RING_BYTES
    volatile int dec_done;               // no more PCM will come
    volatile int quit;
    SDL_Thread *thread;
    uint32_t start_ms;
    uint64_t skip, left;                 // input samples to drop at the start / still allowed (end padding)
    volatile uint32_t played;            // output frames taken by the hook
    int produced;                        // any PCM decoded
    volatile int ended;                  // the hook reported the end once
    int start_errors;                    // decode errors before the first PCM (bit reservoir after a seek)
    uint8_t mp3buf[16 * 1024] __attribute__((aligned(64)));
    uint8_t pcmbuf[16 * 1152] __attribute__((aligned(64)));
};

static HwTrack slots[2];                 // sceMp3 has two handles: playing + being opened
static SDL_mutex *slot_lock;
static HwTrack *volatile cur;
static int init_state;                   // 0 not tried, 1 ok, -1 failed
static volatile int paused, finished, volume = 100;
static volatile uint32_t starved;

int hwmp3_init(void) {
    if (init_state) return init_state > 0 ? 0 : -1;
    int a = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    int b = sceUtilityLoadModule(PSP_MODULE_AV_MP3);
    int c = sceMp3InitResource();
    log_msg("hwmp3: avcodec=%08X mp3=%08X init=%08X", (unsigned)a, (unsigned)b, (unsigned)c);
    // 0x80020139: module already loaded.
    int ok = (a >= 0 || (unsigned)a == 0x80020139) && (b >= 0 || (unsigned)b == 0x80020139) && c >= 0;
    slot_lock = SDL_CreateMutex();
    init_state = ok ? 1 : -1;
    platform_log_memory("hwmp3");
    return ok ? 0 : -1;
}

void hwmp3_shutdown(void) {
    if (init_state <= 0) return;
    hwmp3_stop();
    sceMp3TermResource();
    sceUtilityUnloadModule(PSP_MODULE_AV_MP3);
    sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
    init_state = 0;                              // the next MP3 loads them again
    log_msg("hwmp3: modules unloaded");
}

static int read_at(SceUID fd, uint32_t pos, void *buf, int len) {
    if (sceIoLseek32(fd, (int)pos, PSP_SEEK_SET) < 0) return -1;
    return sceIoRead(fd, buf, len);
}

static int fill_stream(HwTrack *t) {
    SceUChar8 *dst;
    SceInt32 towrite, srcpos;
    if (sceMp3GetInfoToAddStreamData(t->handle, &dst, &towrite, &srcpos) < 0) return -1;
    int n = towrite > 0 ? read_at(t->fd, (uint32_t)srcpos, dst, towrite) : 0;
    if (n < 0) return -1;
    return sceMp3NotifyAddStreamData(t->handle, n);
}

// Converted PCM (44.1 kHz stereo) from the converter into the ring.
static void move_converted(HwTrack *t) {
    for (;;) {
        uint32_t space = RING_BYTES - (t->wr - t->rd);
        int avail = SDL_AudioStreamAvailable(t->conv);
        uint32_t idx = t->wr % RING_BYTES, n = (uint32_t)avail;
        if (n > space) n = space;
        if (n > RING_BYTES - idx) n = RING_BYTES - idx;   // up to the wrap
        n &= ~3u;
        if (!n) return;
        int got = SDL_AudioStreamGet(t->conv, t->ring + idx, (int)n);
        if (got <= 0) return;
        t->wr += (uint32_t)got;
    }
}

// One sceMp3Decode call. 1 = PCM pushed, 0 = end of track, -1 = decoder error.
static int decode_one(HwTrack *t) {
    if (sceMp3CheckStreamDataNeeded(t->handle) > 0 && fill_stream(t) < 0) return -1;
    SceShort16 *pcm;
    int bytes = sceMp3Decode(t->handle, &pcm);
    if (bytes == 0) return 0;
    if (bytes < 0) {
        // Started mid-file (seek / resume): the first frames reference data before the start
        // (bit reservoir) and cannot be decoded. Skip a few before giving up.
        if (!t->produced && ++t->start_errors <= 8) return 1;
        log_msg("hwmp3: decode error %08X (%s, %d errors before)", (unsigned)bytes,
                t->produced ? "mid-track" : "at start", t->start_errors);
        return t->produced ? 0 : -1;               // mid-track error: end the track like a load failure would
    }
    if (t->start_errors && !t->produced) log_msg("hwmp3: %d undecodable frames skipped at start", t->start_errors);
    t->produced = 1;
    uint64_t samples = (uint64_t)bytes / (2u * (unsigned)t->channels), off = 0;
    if (t->skip) {
        off = samples < t->skip ? samples : t->skip;
        t->skip -= off;
    }
    uint64_t n = samples - off;
    if (t->left != NO_LIMIT) {
        if (n > t->left) n = t->left;
        t->left -= n;
    }
    if (n) SDL_AudioStreamPut(t->conv, pcm + off * (unsigned)t->channels, (int)(n * 2u * (unsigned)t->channels));
    move_converted(t);
    return t->left == 0 ? 0 : 1;
}

static void finish_decoding(HwTrack *t) {
    log_msg("hwmp3: end of stream (%u ms played so far)", (unsigned)(t->start_ms + (uint64_t)t->played * 1000 / OUT_RATE));
    SDL_AudioStreamFlush(t->conv);
    move_converted(t);
    t->dec_done = 1;
}

static int decode_thread(void *arg) {
    HwTrack *t = arg;
    while (!t->quit) {
        if (t->dec_done) {
            move_converted(t);                      // the flushed tail may not have fitted yet
            SDL_Delay(10);
            continue;
        }
        move_converted(t);
        if (RING_BYTES - (t->wr - t->rd) < DECODE_ROOM || SDL_AudioStreamAvailable(t->conv) > DECODE_ROOM) {
            SDL_Delay(5);
            continue;
        }
        int r = decode_one(t);
        if (r <= 0) {
            if (r < 0) log_msg("hwmp3: decode error");
            finish_decoding(t);
        }
    }
    return 0;
}

static void release(HwTrack *t) {
    if (t->thread) {
        t->quit = 1;
        SDL_WaitThread(t->thread, NULL);
        t->thread = NULL;
    }
    if (t->handle >= 0) sceMp3ReleaseMp3Handle(t->handle);
    if (t->fd >= 0) sceIoClose(t->fd);
    if (t->conv) SDL_FreeAudioStream(t->conv);
    t->conv = NULL;
    t->handle = t->fd = -1;
    SDL_LockMutex(slot_lock);
    t->used = 0;
    SDL_UnlockMutex(slot_lock);
}

static HwTrack *take_slot(void) {
    HwTrack *t = NULL;
    SDL_LockMutex(slot_lock);
    for (int i = 0; i < 2 && !t; i++)
        if (!slots[i].used) t = &slots[i];
    if (t) t->used = 1;
    SDL_UnlockMutex(slot_lock);
    return t;
}

static HwTrack *fail(HwTrack *t, const char *path, const char *why) {
    log_msg("hwmp3: %s: %s", why, path);
    release(t);
    return NULL;
}

HwTrack *hwmp3_open(const char *path, uint32_t start_ms) {
    if (init_state <= 0) return NULL;
    HwTrack *t = take_slot();
    if (!t) { log_msg("hwmp3: no free decoder handle"); return NULL; }
    uint8_t *ring = t->ring;
    memset(t, 0, offsetof(HwTrack, mp3buf));
    t->used = 1;
    t->handle = -1;
    t->ring = ring ? ring : malloc(RING_BYTES);
    t->fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (t->fd < 0 || !t->ring) return fail(t, path, "open failed");
    uint32_t size = (uint32_t)sceIoLseek32(t->fd, 0, PSP_SEEK_END);
    uint8_t head[HEAD_BYTES], tag[3] = { 0 };
    if (read_at(t->fd, 0, head, 10) != 10) return fail(t, path, "short file");
    uint32_t id3 = mp3info_id3v2_size(head, 10);
    if (size > 128) read_at(t->fd, size - 128, tag, 3);
    int n = read_at(t->fd, id3, head, HEAD_BYTES);
    if (n <= 0 || mp3info_parse(head, (size_t)n, id3, size, memcmp(tag, "TAG", 3) == 0, &t->info) != 0)
        return fail(t, path, "unsupported header");
    const Mp3Info *m = &t->info;
    uint32_t stream_start = m->audio_start + (m->info_frame ? (uint32_t)m->first_frame_len : 0);
    uint64_t total = m->frames ? (uint64_t)m->frames * (unsigned)m->samples_per_frame : NO_LIMIT;
    int gapless = m->enc_delay > 0 || m->enc_padding > 0;
    t->left = NO_LIMIT;
    if (start_ms) {
        uint32_t off = mp3info_offset_for_ms(m, start_ms);
        n = read_at(t->fd, off, head, HEAD_BYTES);
        int f = n > 0 ? mp3info_find_frame(head, (size_t)n) : -1;
        uint32_t at = f >= 0 ? off + (uint32_t)f : off;   // no frame found: the decoder resyncs itself
        if (at > stream_start) stream_start = at;
        t->start_ms = start_ms;
        if (gapless && total != NO_LIMIT) {
            uint64_t played = (uint64_t)start_ms * (unsigned)m->sample_rate / 1000, valid = total - m->enc_delay - m->enc_padding;
            t->left = played < valid ? valid - played : 0;
        }
    } else if (gapless) {
        t->skip = (uint64_t)m->enc_delay + DECODER_DELAY;
        if (total != NO_LIMIT) t->left = total - m->enc_delay - m->enc_padding;
    }
    SceMp3InitArg arg = { stream_start, m->audio_end, t->mp3buf, sizeof t->mp3buf, t->pcmbuf, sizeof t->pcmbuf };
    t->handle = sceMp3ReserveMp3Handle(&arg);
    if (t->handle < 0) return fail(t, path, "reserve handle failed");
    if (fill_stream(t) < 0 || sceMp3Init(t->handle) < 0) return fail(t, path, "decoder init failed");
    // The decoder loops forever by default: without a LAME length limit the track never ended
    // (author: 5:41 shown on a 5:22 track).
    sceMp3SetLoopNum(t->handle, 0);
    t->channels = sceMp3GetMp3ChannelNum(t->handle);
    t->rate = sceMp3GetSamplingRate(t->handle);
    if (t->channels < 1 || t->channels > 2 || t->rate <= 0) return fail(t, path, "bad stream format");
    if (t->rate != m->sample_rate || t->channels != m->channels)
        log_msg("hwmp3: header says %d Hz/%d ch, decoder %d Hz/%d ch", m->sample_rate, m->channels, t->rate, t->channels);
    t->conv = SDL_NewAudioStream(AUDIO_S16SYS, (Uint8)t->channels, t->rate, AUDIO_S16SYS, 2, OUT_RATE);
    if (!t->conv) return fail(t, path, "converter failed");
    while (t->wr < PREFILL_BYTES) {           // first PCM ready before the switch
        int r = decode_one(t);
        if (r < 0) return fail(t, path, "first decode failed");
        if (r == 0) { finish_decoding(t); break; }
    }
    t->thread = SDL_CreateThread(decode_thread, "nemo_hwmp3", t);
    if (!t->thread) return fail(t, path, "thread failed");
    return t;
}

void hwmp3_discard(HwTrack *t) {
    if (t) release(t);
}

// SDL_mixer audio thread: the stream was cleared to silence before this call.
static void hook(void *udata, Uint8 *stream, int len) {
    HwTrack *t = udata;                          // handed over by Mix_HookMusic under the mixer's lock
    if (!t || paused) return;
    uint32_t avail = t->wr - t->rd, want = (uint32_t)len & ~3u, n = avail < want ? avail : want;
    uint32_t idx = t->rd % RING_BYTES, first = n < RING_BYTES - idx ? n : RING_BYTES - idx;
    memcpy(stream, t->ring + idx, first);
    memcpy(stream + first, t->ring, n - first);
    if (volume < 100) {
        Sint16 *s = (Sint16 *)stream;
        for (uint32_t i = 0; i < n / 2; i++) s[i] = (Sint16)(s[i] * volume / 100);
    }
    t->rd += n;
    t->played += n / 4;
    if (n < want) {
        if (t->dec_done && SDL_AudioStreamAvailable(t->conv) == 0) {
            if (!t->ended) {                     // once per track, or the player would skip ahead
                t->ended = 1;
                finished = 1;
            }
        } else {
            starved++;
        }
    }
}

void hwmp3_start(HwTrack *t) {
    HwTrack *old = cur;
    paused = 0;
    finished = 0;
    Mix_HookMusic(hook, t);                      // locks the mixer's device: the old track is out after this
    cur = t;
    if (old && old != t) release(old);
}

void hwmp3_stop(void) {
    HwTrack *old = cur;
    if (!old) return;
    Mix_HookMusic(NULL, NULL);
    cur = NULL;
    release(old);
    finished = 0;
}

int hwmp3_active(void) { return cur != NULL; }
void hwmp3_pause(int p) { paused = p; }
uint32_t hwmp3_position_ms(void) {
    HwTrack *t = cur;
    return t ? t->start_ms + (uint32_t)((uint64_t)t->played * 1000 / OUT_RATE) : 0;
}
uint32_t hwmp3_duration_ms(void) { return cur ? mp3info_duration_ms(&cur->info) : 0; }
void hwmp3_set_volume(int percent) { volume = percent; }
int hwmp3_take_finished(void) {
    if (!finished || !cur) return 0;
    finished = 0;
    return 1;
}
uint32_t hwmp3_starved(void) { return starved; }
