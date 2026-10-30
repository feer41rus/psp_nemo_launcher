#ifndef NEMO_CCLOG_H
#define NEMO_CCLOG_H

// Control Center log: ms0:/PSP/NEMO/logs/cc.log, one line per call (opened, appended, closed); restarts past 64 KB.
void cclog(const char *fmt, ...);

#endif
