#ifndef HK_TYPES_H
#define HK_TYPES_H

/*
 * hk_types.h — core types for hollowkernel
 *
 * Every subsystem includes this. Nothing in here depends on any
 * other project header — it's the bottom of the dependency tree.
 */

#include <sys/types.h>   /* pid_t                */
#include <stdint.h>      /* uint32_t etc.        */
#include <stdbool.h>     /* bool                 */

/* ── tunables ───────────────────────────────────────────────── */

#define HK_MAX_CONTAINERS  64          /* max live containers   */
#define HK_NAME_MAX        64          /* max container name    */
#define HK_STACK_SIZE      (1024 * 1024)  /* 1MB clone stack   */

/* ── container lifecycle states ─────────────────────────────── */

typedef enum {
    HK_STATE_EMPTY   = 0,  /* slot is free                      */
    HK_STATE_CREATED,      /* created, not yet started          */
    HK_STATE_RUNNING,      /* process is alive                  */
    HK_STATE_STOPPED,      /* exited or killed                  */
} hk_state_t;

/* ── container descriptor ───────────────────────────────────── */

/*
 * One of these exists for every container, stored in a flat
 * array (the "container table") inside container.c
 */
typedef struct {
    int         id;                  /* unique container id      */
    char        name[HK_NAME_MAX];   /* human-readable label     */
    pid_t       pid;                 /* host PID of the process  */
    hk_state_t  state;              /* current lifecycle state   */
    int         priority;            /* 0 (low) – 9 (high)       */
    char        rootfs[256];         /* rootfs path (Phase 4)    */

    /* resource counters — populated in later phases */
    long        mem_bytes;
    long        cpu_ticks;
} hk_container_t;

/* ── return codes ───────────────────────────────────────────── */

/*
 * Every function in this project returns one of these.
 * No magic numbers scattered around the codebase.
 */
typedef enum {
    HK_OK       =  0,
    HK_ERR      = -1,
    HK_FULL     = -2,   /* container table is full              */
    HK_NOTFOUND = -3,   /* no container with that id            */
} hk_rc_t;

#endif /* HK_TYPES_H */