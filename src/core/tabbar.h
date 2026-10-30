#ifndef NEMO_TABBAR_H
#define NEMO_TABBAR_H

// Top tab strip (1.3.1): with more tabs than fit, the strip scrolls so the active tab is fully visible.
// widths: visible tabs only, in order; gap between tabs; avail: room between the L and R glyphs;
// cur: the current offset. Returns the new offset (0 when everything fits).
int tabbar_offset(const int *widths, int n, int active, int avail, int gap, int cur);

#endif
