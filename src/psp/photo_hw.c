// Photos tab on the PSP (1.3.1): decoding (JPEG through libjpeg-turbo, scaled while decoding; PNG/BMP
// through SDL_image), rotation into the file (in memory when it fits, else in bands through the card),
// thumbnails on a worker thread with a card cache, deletion and the one-time move of old screenshots.
#include "photo_hw.h"
#include "bigrot.h"
#include "gfx.h"
#include "log.h"
#include "memory_hw.h"
#include "photofile.h"
#include "photoview.h"
#include "platform.h"
#include <SDL2/SDL_image.h>
#include <jpeglib.h>
#include <png.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <turbojpeg.h>

#define MOVED_MARK "ms0:/PSP/NEMO/photo_moved.txt"
#define THUMB_DIR "ms0:/PSP/NEMO/cache/photo"
#define THUMB_W 84
#define THUMB_H 63
#define MEM_MARGIN (2u * 1024 * 1024)   // kept free for everything else
#define CST_MT 0x0020                    // sceIoChstat: modification time
#define JPEG_QUALITY 92
#define BAND_ROWS 256

static int mk(const char *p) { return sceIoMkdir(p, 0777); }

void photo_move_old_once(void) {
    SceIoStat st;
    if (sceIoGetstat(MOVED_MARK, &st) >= 0) return;
    sceIoMkdir("ms0:/PICTURE", 0777);
    sceIoMkdir("ms0:/PICTURE/Nemo", 0777);
    int games = photofile_move_old("ms0:/PICTURE/Nemo", "ms0:/PICTURE/Nemo/Games", ".bmp", mk);
    int shots = photofile_move_old("ms0:/PSP/NEMO/screenshots", "ms0:/PICTURE/Nemo/Screenshots", ".png", mk);
    sceIoRmdir("ms0:/PSP/NEMO/screenshots");   // only when empty
    int left = photofile_count_ext("ms0:/PICTURE/Nemo", ".bmp") + photofile_count_ext("ms0:/PSP/NEMO/screenshots", ".png");
    log_msg("photo: moved %d game shots, %d Nemo shots, %d left", games, shots, left);
    if (left) return;   // try again at the next start
    FILE *f = fopen(MOVED_MARK, "w");
    if (f) { fputs("1\n", f); fclose(f); }
}

// ---------- helpers ----------

typedef enum { PF_JPEG, PF_PNG, PF_BMP, PF_OTHER } PhotoFmt;

static PhotoFmt fmt_of(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return PF_OTHER;
    if (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg")) return PF_JPEG;
    if (!strcasecmp(dot, ".png")) return PF_PNG;
    if (!strcasecmp(dot, ".bmp")) return PF_BMP;
    return PF_OTHER;
}

static uint32_t mem_free(void) {
    uint32_t f, t;
    platform_memory(&f, &t);
    return f;
}

static int fits(long long bytes) { uint32_t f = mem_free(); return f > MEM_MARGIN && bytes <= (long long)(f - MEM_MARGIN); }

static uint8_t *read_file(const char *path, long max, long *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = NULL;
    if (n > 0 && (max <= 0 || n <= max) && fits(n)) buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) *size = n;
    return buf;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

int photo_size(const char *path, int *w, int *h) {
    PhotoFmt f = fmt_of(path);
    if (f == PF_JPEG) {   // the frame header may sit behind a big EXIF block: read up to 256 KB, then all
        enum { HEAD = 256 * 1024 };
        FILE *fp = fopen(path, "rb");
        uint8_t *head = fp ? malloc(HEAD) : NULL;   // only while reading: no permanent 256 KB
        size_t n = head ? fread(head, 1, HEAD, fp) : 0;
        if (fp) fclose(fp);
        tjhandle tj = head ? tjInitDecompress() : NULL;
        int ss, cs, rc = tj ? tjDecompressHeader3(tj, head, (unsigned long)n, w, h, &ss, &cs) : -1;
        if (tj) tjDestroy(tj);
        free(head);
        return rc == 0 ? 0 : -1;
    }
    uint8_t b[26];
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    size_t n = fread(b, 1, sizeof b, fp);
    fclose(fp);
    if (f == PF_PNG && n >= 24 && !memcmp(b, "\x89PNG", 4)) { *w = (int)be32(b + 16); *h = (int)be32(b + 20); return 0; }
    if (f == PF_BMP && n >= 26 && b[0] == 'B' && b[1] == 'M') {
        *w = (int)le32(b + 18);
        int hh = (int)le32(b + 22);
        *h = hh < 0 ? -hh : hh;
        return 0;
    }
    return -1;
}

// Nearest-neighbour resample of RGB24 rows (pitch in bytes).
static void sample_rgb_pitch(const uint8_t *src, int pitch, SDL_Rect r, uint8_t *dst, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        const uint8_t *row = src + (size_t)(r.y + (int)((long long)y * r.h / dh)) * pitch;
        for (int x = 0; x < dw; x++) memcpy(dst + ((size_t)y * dw + x) * 3, row + (size_t)(r.x + (int)((long long)x * r.w / dw)) * 3, 3);
    }
}

static void sample_rgb(const uint8_t *src, int sw, SDL_Rect r, uint8_t *dst, int dw, int dh) { sample_rgb_pitch(src, sw * 3, r, dst, dw, dh); }

// Any SDL surface into packed RGB24 dst_w x dst_h: one SDL conversion, then byte copies (per-pixel
// SDL_GetRGB took ~220 ms for a 480x272 BMP on the console).
static int sample_surface(SDL_Surface *s, uint8_t *dst, int dst_w, int dst_h) {
    SDL_Surface *rgb = s->format->format == SDL_PIXELFORMAT_RGB24 ? s : SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);
    if (!rgb) return -1;
    SDL_LockSurface(rgb);
    sample_rgb_pitch(rgb->pixels, rgb->pitch, (SDL_Rect){ 0, 0, rgb->w, rgb->h }, dst, dst_w, dst_h);
    SDL_UnlockSurface(rgb);
    if (rgb != s) SDL_FreeSurface(rgb);
    return 0;
}

static void scaled_size(int w, int h, int max_side, int *ow, int *oh) {
    int side = w > h ? w : h;
    if (side <= max_side) { *ow = w; *oh = h; return; }
    *ow = (int)((long long)w * max_side / side);
    *oh = (int)((long long)h * max_side / side);
    if (*ow < 1) *ow = 1;
    if (*oh < 1) *oh = 1;
}

// ---------- viewer ----------

void photo_image_free(PhotoImage *img) {
    free(img->rgb);
    memset(img, 0, sizeof *img);
}

static int load_jpeg(const char *path, int max_side, PhotoImage *out) {
    long size = 0;
    uint8_t *jpg = read_file(path, 0, &size);
    if (!jpg) {
        struct stat st;
        return stat(path, &st) == 0 && !fits((long long)st.st_size) ? -2 : -1;
    }
    tjhandle tj = tjInitDecompress();
    int w, h, ss, cs, rc = -1;
    if (tj && tjDecompressHeader3(tj, jpg, (unsigned long)size, &w, &h, &ss, &cs) == 0) {
        int d = pv_decode_denom(w, h, max_side);
        tjscalingfactor sf = { 1, d };
        int dw = TJSCALED(w, sf), dh = TJSCALED(h, sf), ow, oh;
        scaled_size(dw, dh, max_side, &ow, &oh);
        long long need = (long long)dw * dh * 3 + (ow != dw ? (long long)ow * oh * 3 : 0);
        uint8_t *buf = fits(need) ? malloc((size_t)dw * dh * 3) : NULL;
        rc = buf ? 0 : -2;
        if (buf && tjDecompress2(tj, jpg, (unsigned long)size, buf, dw, 0, dh, TJPF_RGB, TJFLAG_FASTDCT) != 0) {
            free(buf);
            buf = NULL;
            rc = -1;
        }
        if (buf && ow != dw) {   // denominator 8 was not enough: one more nearest-neighbour step
            uint8_t *small = malloc((size_t)ow * oh * 3);
            if (small) sample_rgb(buf, dw, (SDL_Rect){ 0, 0, dw, dh }, small, ow, oh);
            free(buf);
            buf = small;
            if (!buf) rc = -2;
        }
        if (buf) { out->rgb = buf; out->w = ow; out->h = oh; out->full_w = w; out->full_h = h; }
    }
    if (tj) tjDestroy(tj);
    free(jpg);
    return rc;
}

static int load_sdl(const char *path, int max_side, PhotoImage *out) {
    int w, h;
    if (photo_size(path, &w, &h) != 0) return -1;
    int ow, oh;
    scaled_size(w, h, max_side, &ow, &oh);
    if (!fits((long long)w * h * 7 + (long long)ow * oh * 3)) return -2;   // loaded + RGB24 copy + result
    SDL_Surface *s = IMG_Load(path);
    if (!s) return -1;
    uint8_t *buf = malloc((size_t)ow * oh * 3);
    if (buf && sample_surface(s, buf, ow, oh) != 0) { free(buf); buf = NULL; }
    SDL_FreeSurface(s);
    if (!buf) return -2;
    out->rgb = buf; out->w = ow; out->h = oh; out->full_w = w; out->full_h = h;
    return 0;
}

int photo_load(const char *path, int max_side, PhotoImage *out) {
    memset(out, 0, sizeof *out);
    uint32_t t0 = platform_ms();
    PhotoFmt f = fmt_of(path);
    int rc = f == PF_JPEG ? load_jpeg(path, max_side, out) : f == PF_OTHER ? -1 : load_sdl(path, max_side, out);
    log_msg("photo: load %s rc=%d %dx%d -> %dx%d, %u ms", path, rc, out->full_w, out->full_h, out->w, out->h,
            (unsigned)(platform_ms() - t0));
    return rc;
}

// ---------- rotation in memory ----------

typedef struct { const uint8_t *data; unsigned long size; SDL_Surface *surf; PhotoFmt fmt; } WriteCtx;

static int write_surface(SDL_Surface *s, PhotoFmt fmt, const char *tmp);   // PNG/BMP through the row codecs

static int write_tmp(void *ctx, const char *tmp) {
    WriteCtx *c = ctx;
    if (c->surf) return write_surface(c->surf, c->fmt, tmp);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = fwrite(c->data, 1, c->size, f) == c->size;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

static void keep_date(const char *path, const SceIoStat *before) {
    SceIoStat st = *before;
    sceIoChstat(path, &st, CST_MT);
}

static PhotoRotateResult rotate_jpeg(const char *path, int cw) {
    long size = 0;
    uint8_t *jpg = read_file(path, 0, &size);
    if (!jpg) return PR_FAIL;
    tjhandle tj = tjInitDecompress();
    int w = 0, h = 0, ss = TJSAMP_420, cs, res = PR_FAIL;
    if (!tj || tjDecompressHeader3(tj, jpg, (unsigned long)size, &w, &h, &ss, &cs) != 0) goto out;
    if (fits((long long)w * h * 3 + 3LL * size)) {   // lossless first: only when the size is a whole number of blocks
        tjhandle tx = tjInitTransform();
        unsigned char *dst = NULL;
        unsigned long dsize = 0;
        tjtransform t;
        memset(&t, 0, sizeof t);
        t.op = cw ? TJXOP_ROT90 : TJXOP_ROT270;
        t.options = TJXOPT_PERFECT | TJXOPT_COPYNONE;   // no EXIF: its thumbnail and orientation would be stale
        if (tx && tjTransform(tx, jpg, (unsigned long)size, 1, &dst, &dsize, &t, 0) == 0) {
            WriteCtx c = { dst, dsize, NULL, PF_JPEG };
            res = photofile_replace(path, write_tmp, &c) == 0 ? PR_OK : PR_FAIL;
            log_msg("photo: rotate %s lossless %dx%d", path, w, h);
        }
        if (dst) tjFree(dst);
        if (tx) tjDestroy(tx);
        if (res == PR_OK || dsize) goto out;
    }
    if (!fits((long long)w * h * 6 + size)) { res = PR_BIG; goto out; }
    {   // re-encode
        uint8_t *a = malloc((size_t)w * h * 3), *b = a ? malloc((size_t)w * h * 3) : NULL;
        if (a && b && tjDecompress2(tj, jpg, (unsigned long)size, a, w, 0, h, TJPF_RGB, 0) == 0 &&
            pv_rotate(a, w, h, 3, cw, b) == 0) {
            free(a);
            a = NULL;
            tjhandle tc = tjInitCompress();
            unsigned char *dst = NULL;
            unsigned long dsize = 0;
            if (tc && tjCompress2(tc, b, h, 0, w, TJPF_RGB, &dst, &dsize, ss, JPEG_QUALITY, 0) == 0) {
                WriteCtx c = { dst, dsize, NULL, PF_JPEG };
                res = photofile_replace(path, write_tmp, &c) == 0 ? PR_OK : PR_FAIL;
                log_msg("photo: rotate %s re-encoded %dx%d", path, w, h);
            }
            if (dst) tjFree(dst);
            if (tc) tjDestroy(tc);
        }
        free(a);
        free(b);
    }
out:
    if (tj) tjDestroy(tj);
    free(jpg);
    return (PhotoRotateResult)res;
}

static PhotoRotateResult rotate_sdl(const char *path, PhotoFmt fmt, int cw) {
    int w, h;
    if (photo_size(path, &w, &h) != 0) return PR_FAIL;
    if (!fits((long long)w * h * 4 * 3)) return PR_BIG;   // loaded, as RGBA, rotated copy
    SDL_Surface *s = IMG_Load(path);
    if (!s) return PR_FAIL;
    SDL_Surface *a = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(s);
    if (!a) return PR_FAIL;
    SDL_Surface *b = SDL_CreateRGBSurfaceWithFormat(0, a->h, a->w, 32, SDL_PIXELFORMAT_ABGR8888);
    PhotoRotateResult res = PR_FAIL;
    if (b && a->pitch == a->w * 4 && b->pitch == b->w * 4 && pv_rotate(a->pixels, a->w, a->h, 4, cw, b->pixels) == 0) {
        SDL_FreeSurface(a);
        a = NULL;
        WriteCtx c = { NULL, 0, b, fmt };   // BMP stays 24-bit, PNG becomes RGB (alpha flattened)
        res = photofile_replace(path, write_tmp, &c) == 0 ? PR_OK : PR_FAIL;
    }
    if (a) SDL_FreeSurface(a);
    if (b) SDL_FreeSurface(b);
    return res;
}

PhotoRotateResult photo_rotate_file(const char *path, int cw) {
    SceIoStat before;
    MemCard card;
    if (sceIoGetstat(path, &before) < 0) return PR_FAIL;
    // The free-space query can take ~0.6 s (the PSP counts free clusters over the whole card), so only
    // photos over 2 MB are checked; a smaller one fails safely if the card is full (the original stays).
    if (before.st_size > 2 * 1024 * 1024 && memory_card_read("ms0:", &card) == 0 &&
        card.free < (long long)before.st_size * 2 + 1024 * 1024)
        return PR_NO_SPACE;
    uint32_t t0 = platform_ms();
    PhotoFmt f = fmt_of(path);
    PhotoRotateResult r = f == PF_JPEG ? rotate_jpeg(path, cw) : f == PF_OTHER ? PR_FAIL : rotate_sdl(path, f, cw);
    if (r == PR_OK) keep_date(path, &before);
    log_msg("photo: rotate %s %s rc=%d %u ms", path, cw ? "cw" : "ccw", r, (unsigned)(platform_ms() - t0));
    return r;
}

// ---------- rotation in bands (large photos) ----------

typedef struct { struct jpeg_error_mgr pub; jmp_buf jb; } JErr;
static void jerr_exit(j_common_ptr c) { longjmp(((JErr *)c->err)->jb, 1); }

typedef struct {
    PhotoFmt fmt;
    FILE *f;
    int w, h, row;
    // JPEG
    struct jpeg_decompress_struct jd;
    struct jpeg_compress_struct jc;
    JErr je;
    // PNG
    png_structp png;
    png_infop info;
    // BMP
    long data_off, row_size;
    int bottom_up;
    uint8_t *bmp_row;
    int bmp_seq;   // sink: rows arrive bottom-up, written in order (no seek per row)
} Codec;

#define IO_BUF (64 * 1024)   // one card write per 64 KB instead of one per row

static int src_read_row(void *ctx, uint8_t *row) {
    Codec *c = ctx;
    if (c->fmt == PF_JPEG) {
        if (setjmp(c->je.jb)) return -1;
        JSAMPROW r = row;
        return jpeg_read_scanlines(&c->jd, &r, 1) == 1 ? 0 : -1;
    }
    if (c->fmt == PF_PNG) {
        if (setjmp(png_jmpbuf(c->png))) return -1;
        png_read_row(c->png, row, NULL);
        return 0;
    }
    long y = c->bottom_up ? c->h - 1 - c->row : c->row;   // BMP: 24-bit BGR
    c->row++;
    if (fseek(c->f, c->data_off + y * c->row_size, SEEK_SET) != 0 || fread(c->bmp_row, 1, (size_t)c->w * 3, c->f) != (size_t)c->w * 3)
        return -1;
    for (int x = 0; x < c->w; x++) { row[3 * x] = c->bmp_row[3 * x + 2]; row[3 * x + 1] = c->bmp_row[3 * x + 1]; row[3 * x + 2] = c->bmp_row[3 * x]; }
    return 0;
}

static int src_open(Codec *c, const char *path) {
    c->f = fopen(path, "rb");
    if (!c->f) return -1;
    setvbuf(c->f, NULL, _IOFBF, IO_BUF);
    if (c->fmt == PF_JPEG) {
        c->jd.err = jpeg_std_error(&c->je.pub);
        c->je.pub.error_exit = jerr_exit;
        if (setjmp(c->je.jb)) return -1;
        jpeg_create_decompress(&c->jd);
        jpeg_stdio_src(&c->jd, c->f);
        jpeg_read_header(&c->jd, TRUE);
        c->jd.out_color_space = JCS_RGB;
        jpeg_start_decompress(&c->jd);
        c->w = (int)c->jd.output_width;
        c->h = (int)c->jd.output_height;
        return 0;
    }
    if (c->fmt == PF_PNG) {
        c->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        c->info = c->png ? png_create_info_struct(c->png) : NULL;
        if (!c->info || setjmp(png_jmpbuf(c->png))) return -1;
        png_init_io(c->png, c->f);
        png_read_info(c->png, c->info);
        if (png_get_interlace_type(c->png, c->info) != PNG_INTERLACE_NONE) return -1;   // needs whole passes
        png_set_expand(c->png);
        png_set_strip_16(c->png);
        png_set_strip_alpha(c->png);
        png_set_gray_to_rgb(c->png);
        png_read_update_info(c->png, c->info);
        c->w = (int)png_get_image_width(c->png, c->info);
        c->h = (int)png_get_image_height(c->png, c->info);
        return png_get_rowbytes(c->png, c->info) == (size_t)c->w * 3 ? 0 : -1;
    }
    uint8_t b[54];
    if (fread(b, 1, sizeof b, c->f) != sizeof b || b[0] != 'B' || b[1] != 'M') return -1;
    if (b[28] != 24 || le32(b + 30) != 0) return -1;   // 24-bit, uncompressed only
    c->data_off = (long)le32(b + 10);
    c->w = (int)le32(b + 18);
    int hh = (int)le32(b + 22);
    c->bottom_up = hh > 0;
    c->h = hh < 0 ? -hh : hh;
    c->row_size = ((long)c->w * 3 + 3) & ~3L;
    c->bmp_row = malloc((size_t)c->row_size);
    return c->bmp_row ? 0 : -1;
}

static void src_close(Codec *c) {
    if (c->fmt == PF_JPEG && c->jd.err) {
        if (!setjmp(c->je.jb)) jpeg_destroy_decompress(&c->jd);
    }
    if (c->png) png_destroy_read_struct(&c->png, c->info ? &c->info : NULL, NULL);
    free(c->bmp_row);
    if (c->f) fclose(c->f);
}

static int dst_write_row(void *ctx, const uint8_t *row) {
    Codec *c = ctx;
    if (c->fmt == PF_JPEG) {
        if (setjmp(c->je.jb)) return -1;
        JSAMPROW r = (JSAMPROW)row;
        return jpeg_write_scanlines(&c->jc, &r, 1) == 1 ? 0 : -1;
    }
    if (c->fmt == PF_PNG) {
        if (setjmp(png_jmpbuf(c->png))) return -1;
        png_write_row(c->png, row);
        return 0;
    }
    for (int x = 0; x < c->w; x++) { c->bmp_row[3 * x] = row[3 * x + 2]; c->bmp_row[3 * x + 1] = row[3 * x + 1]; c->bmp_row[3 * x + 2] = row[3 * x]; }
    long y = c->h - 1 - c->row;   // bottom-up
    c->row++;
    if (c->bmp_seq) return fwrite(c->bmp_row, 1, (size_t)c->row_size, c->f) == (size_t)c->row_size ? 0 : -1;
    return fseek(c->f, 54 + y * c->row_size, SEEK_SET) == 0 && fwrite(c->bmp_row, 1, (size_t)c->row_size, c->f) == (size_t)c->row_size ? 0 : -1;
}

static int dst_open(Codec *c, const char *tmp) {
    c->f = fopen(tmp, "wb");
    if (!c->f) return -1;
    setvbuf(c->f, NULL, _IOFBF, IO_BUF);
    if (c->fmt == PF_JPEG) {
        c->jc.err = jpeg_std_error(&c->je.pub);
        c->je.pub.error_exit = jerr_exit;
        if (setjmp(c->je.jb)) return -1;
        jpeg_create_compress(&c->jc);
        jpeg_stdio_dest(&c->jc, c->f);
        c->jc.image_width = (JDIMENSION)c->w;
        c->jc.image_height = (JDIMENSION)c->h;
        c->jc.input_components = 3;
        c->jc.in_color_space = JCS_RGB;
        jpeg_set_defaults(&c->jc);
        jpeg_set_quality(&c->jc, JPEG_QUALITY, TRUE);
        jpeg_start_compress(&c->jc, TRUE);
        return 0;
    }
    if (c->fmt == PF_PNG) {
        c->png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        c->info = c->png ? png_create_info_struct(c->png) : NULL;
        if (!c->info || setjmp(png_jmpbuf(c->png))) return -1;
        png_init_io(c->png, c->f);
        png_set_IHDR(c->png, c->info, (png_uint_32)c->w, (png_uint_32)c->h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_write_info(c->png, c->info);
        return 0;
    }
    c->row_size = ((long)c->w * 3 + 3) & ~3L;
    c->bmp_row = calloc(1, (size_t)c->row_size);
    if (!c->bmp_row) return -1;
    uint8_t b[54] = { 'B', 'M' };
    uint32_t data = (uint32_t)(c->row_size * c->h), v[] = { 54 + data, 0, 54, 40, (uint32_t)c->w, (uint32_t)c->h };
    for (int i = 0; i < 6; i++) memcpy(b + 2 + 4 * i, &v[i], 4);   // file size, reserved, offset, DIB size, w, h
    b[26] = 1;
    b[28] = 24;
    memcpy(b + 34, &data, 4);
    return fwrite(b, 1, sizeof b, c->f) == sizeof b ? 0 : -1;
}

static int dst_close(Codec *c, int ok) {
    if (c->fmt == PF_JPEG && c->jc.err) {
        if (!setjmp(c->je.jb)) {
            if (ok) jpeg_finish_compress(&c->jc);
            jpeg_destroy_compress(&c->jc);
        } else {
            ok = 0;
        }
    }
    if (c->png) {
        if (ok) {
            if (setjmp(png_jmpbuf(c->png))) ok = 0;
            else png_write_end(c->png, NULL);
        }
        png_destroy_write_struct(&c->png, c->info ? &c->info : NULL);
    }
    free(c->bmp_row);
    if (c->f && fclose(c->f) != 0) ok = 0;
    return ok ? 0 : -1;
}

// The rotated ABGR8888 surface into a 24-bit BMP or an RGB PNG, buffered (SDL's own savers wrote row
// by row: ~3 s for a 480x272 screenshot on the console).
static int write_surface(SDL_Surface *s, PhotoFmt fmt, const char *tmp) {
    Codec *c = calloc(1, sizeof *c);
    uint8_t *row = malloc((size_t)s->w * 3);
    int ok = c && row;
    if (ok) {
        c->fmt = fmt;
        c->w = s->w;
        c->h = s->h;
        c->bmp_seq = fmt == PF_BMP;   // feed BMP rows bottom-up: one sequential write
        ok = dst_open(c, tmp) == 0;
        for (int i = 0; ok && i < s->h; i++) {
            int y = fmt == PF_BMP ? s->h - 1 - i : i;
            const uint8_t *src = (const uint8_t *)s->pixels + (size_t)y * s->pitch;
            for (int x = 0; x < s->w; x++) memcpy(row + 3 * x, src + 4 * x, 3);   // ABGR8888 in memory: R, G, B, A
            ok = dst_write_row(c, row) == 0;
        }
        if (dst_close(c, ok) != 0) ok = 0;
    }
    free(row);
    free(c);
    return ok ? 0 : -1;
}

static struct {
    char path[256];
    int w, h, cw;
    volatile int state;   // -1 running, else PhotoRotateResult — written only when the thread is done
    BrProgress pr;
    int cancelled;        // seen by the thread itself
} big;

static int big_write(void *ctx, const char *tmp) {
    (void)ctx;
    Codec *src = calloc(1, sizeof *src), *dst = calloc(1, sizeof *dst);
    int rc = -1;
    if (src && dst) {
        src->fmt = dst->fmt = fmt_of(big.path);
        if (src_open(src, big.path) == 0) {
            dst->w = src->h;   // the result is turned on its side
            dst->h = src->w;
            if (dst_open(dst, tmp) == 0) {
                char raw[300];
                snprintf(raw, sizeof raw, "%s.rot", big.path);
                BrSource bs = { src, src_read_row };
                BrSink bd = { dst, dst_write_row };
                int band = BAND_ROWS;   // a wide panorama gets thinner bands
                while (band > 16 && !fits((long long)band * src->w * 3 + (long long)src->h * 3 + band * 3)) band /= 2;
                rc = bigrot_rotate(&bs, src->w, src->h, big.cw, raw, band, &bd, &big.pr);
            }
            if (dst_close(dst, rc == 0) != 0 && rc == 0) rc = -1;
        }
        src_close(src);
    }
    free(src);
    free(dst);
    if (rc == -2) big.cancelled = 1;
    return rc == 0 ? 0 : -1;
}

static int big_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    uint32_t t0 = platform_ms();
    SceIoStat before;
    int have = sceIoGetstat(big.path, &before) >= 0;
    int rc = photofile_replace(big.path, big_write, NULL);
    if (rc == 0 && have) keep_date(big.path, &before);
    PhotoRotateResult r = rc == 0 ? PR_OK : big.cancelled ? PR_CANCELLED : PR_FAIL;
    log_msg("photo: big rotate %dx%d %s %s, %u ms", big.w, big.h, big.path,
            r == PR_OK ? "done" : r == PR_CANCELLED ? "cancelled" : "failed", (unsigned)(platform_ms() - t0));
    big.state = r;
    sceKernelExitDeleteThread(0);
    return 0;
}

PhotoRotateResult photo_rotate_big_start(const char *path, int w, int h, int cw) {
    if (big.state == -1) return PR_FAIL;   // one at a time
    SceIoStat st;
    MemCard card;
    if (sceIoGetstat(path, &st) < 0 || memory_card_read("ms0:", &card) != 0) return PR_FAIL;
    if (card.free < bigrot_need_bytes(w, h, (long long)st.st_size) + 1024 * 1024) return PR_NO_SPACE;
    snprintf(big.path, sizeof big.path, "%s", path);
    big.w = w;
    big.h = h;
    big.cw = cw;
    big.pr.percent = 0;
    big.pr.cancel = 0;
    big.cancelled = 0;
    big.state = -1;
    SceUID th = sceKernelCreateThread("nemo_photo_rot", big_thread, 0x50, 0x20000, PSP_THREAD_ATTR_USER, NULL);
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
        big.state = PR_FAIL;
        return PR_FAIL;
    }
    return PR_OK;
}

int photo_rotate_big_state(int *percent) {
    if (percent) *percent = big.pr.percent;
    return big.state;
}

void photo_rotate_big_cancel(void) { big.pr.cancel = 1; }

void photo_leftover(const char *path, void *ctx) {
    (void)ctx;
    if (big.state == -1) return;   // a rotation running now owns its .tmp/.rot
    int restored = photofile_recover(path);
    log_msg("photo: leftover %s %s", path, restored ? "restored as the photo" : "removed");
}

// ---------- thumbnails ----------

#define TQ_MAX 32
#define TEX_MAX 40
#define FAIL_MAX 64

typedef struct { char key[17]; char path[256]; } ThumbJob;
typedef struct { char key[17]; SDL_Texture *tex; uint32_t used; } ThumbTex;

static SceUID tq_lock = -1, tq_work = -1;
static volatile int paused;   // the viewer or a rotation needs the memory and the card
static ThumbJob queue[TQ_MAX];
static int q_count;
static ThumbJob busy_job;
static int busy;
static SDL_Surface *ready_surf;   // one finished thumbnail waiting for the main thread
static char ready_key[17];
static int ready_failed;
static ThumbTex texs[TEX_MAX];
static char failed[FAIL_MAX][17];
static int fail_next;
static uint32_t tick;

static SDL_Surface *make_thumb(const char *path, int *nomem) {
    *nomem = 0;
    uint8_t *rgb = NULL;
    int w = 0, h = 0;
    PhotoFmt f = fmt_of(path);
    if (f == PF_JPEG) {
        long size = 0;
        uint8_t *jpg = read_file(path, 0, &size);
        if (!jpg) { *nomem = 1; return NULL; }   // unreadable or no room: try again later, not "failed"
        tjhandle tj = tjInitDecompress();
        int fw, fh, ss, cs;
        if (tj && tjDecompressHeader3(tj, jpg, (unsigned long)size, &fw, &fh, &ss, &cs) == 0) {
            int d = 8;   // the most a thumbnail can be reduced and still cover THUMB_W x THUMB_H
            while (d > 1 && ((fw + d - 1) / d < THUMB_W || (fh + d - 1) / d < THUMB_H)) d /= 2;
            tjscalingfactor sf = { 1, d };
            w = TJSCALED(fw, sf);
            h = TJSCALED(fh, sf);
            if (!fits((long long)w * h * 3)) *nomem = 1;
            rgb = *nomem ? NULL : malloc((size_t)w * h * 3);
            if (rgb && tjDecompress2(tj, jpg, (unsigned long)size, rgb, w, 0, h, TJPF_RGB, TJFLAG_FASTDCT) != 0) {
                free(rgb);
                rgb = NULL;
            }
        }
        if (tj) tjDestroy(tj);
        free(jpg);
    } else if (f != PF_OTHER) {
        int fw, fh;
        if (photo_size(path, &fw, &fh) != 0) return NULL;
        if (!fits((long long)fw * fh * 7)) { *nomem = 1; return NULL; }
        SDL_Surface *s = IMG_Load(path);
        if (s) {
            w = s->w < 512 ? s->w : 512;   // enough to cut a thumbnail from
            h = (int)((long long)s->h * w / s->w);
            if (h < 1) h = 1;
            rgb = malloc((size_t)w * h * 3);
            if (rgb && sample_surface(s, rgb, w, h) != 0) { free(rgb); rgb = NULL; }
            SDL_FreeSurface(s);
        }
    }
    if (!rgb) return NULL;
    // Centre crop to 4:3, then down to THUMB_W x THUMB_H.
    SDL_Rect r = { 0, 0, w, h };
    if ((long long)w * THUMB_H > (long long)h * THUMB_W) { r.w = (int)((long long)h * THUMB_W / THUMB_H); r.x = (w - r.w) / 2; }
    else { r.h = (int)((long long)w * THUMB_H / THUMB_W); r.y = (h - r.h) / 2; }
    SDL_Surface *t = SDL_CreateRGBSurfaceWithFormat(0, THUMB_W, THUMB_H, 24, SDL_PIXELFORMAT_RGB24);
    if (t) {
        static uint8_t small[THUMB_W * THUMB_H * 3];
        sample_rgb(rgb, w, r, small, THUMB_W, THUMB_H);
        for (int y = 0; y < THUMB_H; y++) memcpy((uint8_t *)t->pixels + (size_t)y * t->pitch, small + (size_t)y * THUMB_W * 3, THUMB_W * 3);
    }
    free(rgb);
    return t;
}

static int thumb_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (;;) {
        sceKernelWaitSema(tq_work, 1, NULL);
        while (paused) sceKernelDelayThread(20000);
        sceKernelWaitSema(tq_lock, 1, NULL);
        if (!q_count || busy) { sceKernelSignalSema(tq_lock, 1); continue; }
        busy_job = queue[--q_count];   // newest request first: what is on screen now
        busy = 1;
        sceKernelSignalSema(tq_lock, 1);

        char cache[300];
        snprintf(cache, sizeof cache, THUMB_DIR "/%s.png", busy_job.key);
        SDL_Surface *s = IMG_Load(cache);
        int nomem = 0;
        if (!s) {
            s = make_thumb(busy_job.path, &nomem);
            if (s && !paused) IMG_SavePNG(s, cache);   // not while USB may be about to take the stick
        }
        sceKernelWaitSema(tq_lock, 1, NULL);
        ready_surf = s;
        ready_failed = !s && !nomem;   // out of memory is not a broken photo: asked again later
        memcpy(ready_key, busy_job.key, sizeof ready_key);
        sceKernelSignalSema(tq_lock, 1);
        while (ready_surf || ready_failed) sceKernelDelayThread(10000);   // the main thread takes it (or stop drops it)
        sceKernelWaitSema(tq_lock, 1, NULL);
        busy = 0;
        int more = q_count;
        sceKernelSignalSema(tq_lock, 1);
        if (more) sceKernelSignalSema(tq_work, 1);
    }
    return 0;
}

static void thumbs_init(void) {
    if (tq_lock >= 0) return;
    sceIoMkdir("ms0:/PSP/NEMO/cache", 0777);
    sceIoMkdir(THUMB_DIR, 0777);
    tq_lock = sceKernelCreateSema("nemo_thumb_lock", 0, 1, 1, NULL);
    tq_work = sceKernelCreateSema("nemo_thumb_work", 0, 0, 1000, NULL);
    SceUID th = sceKernelCreateThread("nemo_thumbs", thumb_thread, 0x50, 0x20000, PSP_THREAD_ATTR_USER, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
}

SDL_Texture *photo_thumb(const Photo *p) {
    char key[24];
    photo_thumb_key(p, key, sizeof key);
    for (int i = 0; i < TEX_MAX; i++)
        if (texs[i].tex && !strcmp(texs[i].key, key)) { texs[i].used = tick; return texs[i].tex; }
    for (int i = 0; i < FAIL_MAX; i++)
        if (!strcmp(failed[i], key)) return NULL;
    thumbs_init();
    sceKernelWaitSema(tq_lock, 1, NULL);
    int known = busy && !strcmp(busy_job.key, key);
    for (int i = 0; i < q_count && !known; i++)
        if (!strcmp(queue[i].key, key)) {   // asked again: move it to the top
            ThumbJob j = queue[i];
            memmove(&queue[i], &queue[i + 1], (size_t)(q_count - i - 1) * sizeof *queue);
            queue[q_count - 1] = j;
            known = 1;
        }
    if (!known) {
        if (q_count == TQ_MAX) { memmove(&queue[0], &queue[1], (TQ_MAX - 1) * sizeof *queue); q_count--; }   // drop the oldest
        memcpy(queue[q_count].key, key, 17);
        snprintf(queue[q_count].path, sizeof queue[q_count].path, "%s", p->path);
        q_count++;
    }
    sceKernelSignalSema(tq_lock, 1);
    if (!known) sceKernelSignalSema(tq_work, 1);
    return NULL;
}

void photo_thumbs_tick(void) {
    tick++;
    if (tq_lock < 0 || (!ready_surf && !ready_failed)) return;
    sceKernelWaitSema(tq_lock, 1, NULL);
    SDL_Surface *s = ready_surf;
    char key[17];
    memcpy(key, ready_key, sizeof key);
    int failed_now = ready_failed;
    sceKernelSignalSema(tq_lock, 1);
    if (failed_now) {
        memcpy(failed[fail_next], key, sizeof key);
        fail_next = (fail_next + 1) % FAIL_MAX;
    } else if (s) {
        int slot = 0;   // free slot, else the least recently used
        for (int i = 0; i < TEX_MAX; i++) {
            if (!texs[i].tex) { slot = i; break; }
            if (texs[i].used < texs[slot].used) slot = i;
        }
        if (texs[slot].tex) SDL_DestroyTexture(texs[slot].tex);
        texs[slot].tex = SDL_CreateTextureFromSurface(gfx_renderer(), s);
        memcpy(texs[slot].key, key, sizeof key);
        texs[slot].used = tick;
        SDL_FreeSurface(s);
    }
    sceKernelWaitSema(tq_lock, 1, NULL);
    ready_surf = NULL;
    ready_failed = 0;
    sceKernelSignalSema(tq_lock, 1);
}

void photo_thumbs_pause(int on) { paused = on; }

int photo_thumbs_busy(void) { return tq_lock >= 0 && (busy || q_count || ready_surf); }

void photo_thumbs_stop(void) {
    if (tq_lock >= 0) {
        sceKernelWaitSema(tq_lock, 1, NULL);
        q_count = 0;
        if (ready_surf) SDL_FreeSurface(ready_surf);   // the worker is waiting for it to be taken
        ready_surf = NULL;
        ready_failed = 0;
        sceKernelSignalSema(tq_lock, 1);
    }
    memset(failed, 0, sizeof failed);
    for (int i = 0; i < TEX_MAX; i++) {
        if (texs[i].tex) SDL_DestroyTexture(texs[i].tex);
        texs[i].tex = NULL;
        texs[i].key[0] = '\0';
    }
}

int photo_delete(const Photo *p) {
    char key[24], cache[300];
    photo_thumb_key(p, key, sizeof key);
    snprintf(cache, sizeof cache, THUMB_DIR "/%s.png", key);
    remove(cache);
    for (int i = 0; i < TEX_MAX; i++)
        if (texs[i].tex && !strcmp(texs[i].key, key)) { SDL_DestroyTexture(texs[i].tex); texs[i].tex = NULL; texs[i].key[0] = '\0'; }
    int rc = remove(p->path);
    log_msg("photo: delete %s rc=%d", p->path, rc);
    return rc == 0 ? 0 : -1;
}
