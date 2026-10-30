#include "cclog.h"
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspsysclib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CC_LOG "ms0:/PSP/NEMO/logs/cc.log"   // 1.3.3: with Nemo's logs (Nemo makes the folder)
#define CC_LOG_MAX (64 * 1024)

typedef struct { char *buf; int len, cap; } Out;

static void put_ch(void *ctx, int ch) {   // prnt() feeds characters one by one (the kernel libc has no vsnprintf)
    Out *o = ctx;
    if (ch >= 0 && ch < 0x100 && o->len < o->cap - 1) o->buf[o->len++] = (char)ch;   // 0x200 / 0x201 mark start and end
}

void cclog(const char *fmt, ...) {
    char line[160];
    Out o = { line, 0, (int)sizeof line - 1 };
    va_list ap;
    va_start(ap, fmt);
    prnt(put_ch, &o, fmt, ap);
    va_end(ap);
    line[o.len] = 0;
    strcat(line, "\n");
    SceIoStat st;
    int flags = PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND;
    if (sceIoGetstat(CC_LOG, &st) >= 0 && st.st_size > CC_LOG_MAX) flags = PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC;
    SceUID fd = sceIoOpen(CC_LOG, flags, 0777);
    if (fd < 0) return;
    sceIoWrite(fd, line, strlen(line));
    sceIoClose(fd);
}
