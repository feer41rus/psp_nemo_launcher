#ifndef NEMO_LOWSPACE_H
#define NEMO_LOWSPACE_H

// "Low on space" badge on Home (1.3.10): low under 1 GB or 10 % free, critical under 200 MB.
typedef enum { LOWSPACE_OK, LOWSPACE_LOW, LOWSPACE_CRIT } LowSpace;

LowSpace lowspace_level(long long free_bytes, long long total_bytes);   // unknown sizes (<= 0) are OK
LowSpace lowspace_worst(LowSpace a, LowSpace b);

#endif
