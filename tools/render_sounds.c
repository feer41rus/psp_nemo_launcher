// Writes Nemo's interface sounds as WAV files for listening on a PC (1.2.3): make sounds -> build/sounds/
#include "../src/core/synth.h"
#include <stdio.h>
#include <stdlib.h>

static void le32(FILE *f, unsigned v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xff, f); }
static void le16(FILE *f, unsigned v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }

int main(int argc, char **argv) {
    static const char *NAMES[UIS_COUNT] = { "", "move", "confirm", "back", "tab", "edge", "menu", "chime", "settings_move" };
    const char *dir = argc > 1 ? argv[1] : "build/sounds";
    for (int s = UIS_MOVE; s < UIS_COUNT; s++) {
        int n = synth_length((UiSound)s);
        int16_t *b = malloc(sizeof(int16_t) * (size_t)n);
        synth_render((UiSound)s, b, n);
        char path[256];
        snprintf(path, sizeof path, "%s/%s.wav", dir, NAMES[s]);
        FILE *f = fopen(path, "wb");
        if (!f) { perror(path); return 1; }
        fputs("RIFF", f); le32(f, 36 + 2 * (unsigned)n); fputs("WAVEfmt ", f);
        le32(f, 16); le16(f, 1); le16(f, 1); le32(f, SYNTH_RATE); le32(f, SYNTH_RATE * 2); le16(f, 2); le16(f, 16);
        fputs("data", f); le32(f, 2 * (unsigned)n);
        for (int i = 0; i < n; i++) le16(f, (unsigned)(uint16_t)b[i]);
        fclose(f);
        free(b);
    }
    return 0;
}
