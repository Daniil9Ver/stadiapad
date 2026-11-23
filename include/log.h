/*
 * log.h -- Tiny append-only log in %LOCALAPPDATA%\StadiaPad\StadiaPad.log
 */

#ifndef LOG_H
#define LOG_H

#include <wchar.h>

void log_write(const wchar_t *fmt, ...);

#endif /* LOG_H */
