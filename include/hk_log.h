#ifndef HK_LOG_H
#define HK_LOG_H

/*
 * hk_log.h — logging macros for hollowkernel
 *
 * Four levels: INFO, OK, WARN, ERR
 * Each prints with a colour prefix to make output scannable.
 *
 * These are macros not functions — the preprocessor expands them
 * inline so there's zero call overhead and __FILE__/__LINE__ work
 * correctly if we ever add them.
 */

#include <stdio.h>

/* ANSI escape codes for terminal colours */
#define _HK_RESET   "\033[0m"
#define _HK_CYAN    "\033[36m"
#define _HK_GREEN   "\033[32m"
#define _HK_YELLOW  "\033[33m"
#define _HK_RED     "\033[31m"

/*
 * ##__VA_ARGS__ is a GCC extension that handles the case where
 * no extra arguments are passed — it swallows the trailing comma.
 * We compile with -D_GNU_SOURCE so this is always available.
 */
#define HK_INFO(fmt, ...) \
    fprintf(stdout, _HK_CYAN  "[hk]  " _HK_RESET fmt "\n", ##__VA_ARGS__)

#define HK_OK(fmt, ...) \
    fprintf(stdout, _HK_GREEN "[ok]  " _HK_RESET fmt "\n", ##__VA_ARGS__)

#define HK_WARN(fmt, ...) \
    fprintf(stdout, _HK_YELLOW "[warn] " _HK_RESET fmt "\n", ##__VA_ARGS__)

#define HK_ERR(fmt, ...) \
    fprintf(stderr, _HK_RED   "[err] " _HK_RESET fmt "\n", ##__VA_ARGS__)

#endif /* HK_LOG_H */