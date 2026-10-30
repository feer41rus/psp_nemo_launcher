#include "tabbar.h"

int tabbar_offset(const int *widths, int n, int active, int avail, int gap, int cur) {
    int total = 0, x = 0;
    for (int i = 0; i < n; i++) {
        if (i == active) x = total;
        total += widths[i] + (i + 1 < n ? gap : 0);
    }
    if (total <= avail) return 0;
    int off = cur;
    if (active >= 0 && active < n) {
        if (x < off) off = x;                                       // scrolled past its left edge
        if (x + widths[active] > off + avail) off = x + widths[active] - avail;   // past its right edge
    }
    if (off > total - avail) off = total - avail;
    if (off < 0) off = 0;
    return off;
}
