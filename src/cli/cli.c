#include <stdio.h>
#include <stdlib.h>      /* atoi()    */
#include <string.h>      /* strcmp()  */

#include "hk_cli.h"
#include "hk_container.h"
#include "hk_log.h"

/* ── forward declarations of static handlers ─────────────────
 *
 * These are private to this file. The only public symbol is
 * hk_cli_dispatch() declared in hk_cli.h
 * ─────────────────────────────────────────────────────────── */
static int _cmd_run(int argc, char *argv[]);
static int _cmd_ps(void);
static int _cmd_kill(int argc, char *argv[]);

/* ── usage / banner ──────────────────────────────────────────── */

void hk_cli_usage(void)
{
    printf("\n");
    printf("  \033[36m┌─────────────────────────────────────┐\033[0m\n");
    printf("  \033[36m│        hollowkernel  runtime        │\033[0m\n");
    printf("  \033[36m└─────────────────────────────────────┘\033[0m\n\n");

    printf("  \033[1mUsage:\033[0m  hollowkernel <command> [options]\n\n");

    printf("  \033[1mCommands:\033[0m\n");
    printf("    run <name> [priority 0-9] -- <cmd> [args...]\n");
    printf("        Spawn a new isolated container.\n\n");
    printf("    ps\n");
    printf("        List all containers and their states.\n\n");
    printf("    kill <id>\n");
    printf("        Terminate a running container by id.\n\n");
    printf("    help\n");
    printf("        Show this message.\n\n");

    printf("  \033[2mExamples:\033[0m\n");
    printf("    sudo ./build/hollowkernel run web 5 -- /bin/sh\n");
    printf("    sudo ./build/hollowkernel ps\n");
    printf("    sudo ./build/hollowkernel kill 1\n\n");
}

/* ── command handlers ────────────────────────────────────────── */

/*
 * _cmd_run — handle: run <name> [priority] -- <cmd> [args...]
 *
 * argv here is shifted: argv[0] == "run", argv[1] == name, ...
 * We scan for "--" to find where our args end and the container
 * command begins.
 */
static int _cmd_run(int argc, char *argv[])
{
    if (argc < 2) {
        HK_ERR("usage: run <name> [priority 0-9] -- <cmd> [args...]");
        return 1;
    }

    const char *name = argv[1];
    int priority     = 5;    /* default mid-range priority */
    int sep_idx      = -1;   /* index of "--" in argv      */

    /* Scan for the "--" separator */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            sep_idx = i;
            break;
        }
    }

    if (sep_idx < 0) {
        HK_ERR("missing '--' before the container command");
        HK_ERR("usage: run <name> [priority] -- <cmd> [args...]");
        return 1;
    }

    /*
     * If there's exactly one token between name and "--",
     * treat it as the priority value.
     *
     * argv layout when priority is given:
     *   [0]="run"  [1]="web"  [2]="7"  [3]="--"  [4]="/bin/sh"
     *    sep_idx == 3, so sep_idx - 2 == 1 token between name and --
     */
    if (sep_idx == 3) {
        priority = atoi(argv[2]);
        if (priority < 0 || priority > 9) {
            HK_ERR("priority must be 0-9, got '%s'", argv[2]);
            return 1;
        }
    }

    /* Everything after "--" is the command to run inside the container */
    char **cmd = &argv[sep_idx + 1];
    if (cmd[0] == NULL) {
        HK_ERR("no command specified after '--'");
        return 1;
    }

    int id = hk_container_run(name, cmd, priority);
    return (id > 0) ? 0 : 1;
}

/*
 * _cmd_ps — handle: ps
 *
 * Reap finished containers first so states are accurate,
 * then print the table.
 */
static int _cmd_ps(void)
{
    hk_container_reap();
    hk_container_ps();
    return 0;
}

/*
 * _cmd_kill — handle: kill <id>
 */
static int _cmd_kill(int argc, char *argv[])
{
    if (argc < 2) {
        HK_ERR("usage: kill <id>");
        return 1;
    }

    int id = atoi(argv[1]);
    if (id <= 0) {
        HK_ERR("invalid id '%s' — must be a positive integer", argv[1]);
        return 1;
    }

    return (hk_container_kill(id) == HK_OK) ? 0 : 1;
}

/* ── public dispatcher ───────────────────────────────────────── */

/*
 * hk_cli_dispatch — the only public symbol in this file
 *
 * Receives the raw argc/argv from main(), looks at argv[1]
 * and routes to the right static handler.
 *
 * We pass (argc - 1, argv + 1) to handlers so they see
 * their own command as argv[0] — cleaner than passing offsets.
 */
int hk_cli_dispatch(int argc, char *argv[])
{
    if (argc < 2) {
        hk_cli_usage();
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "run")  == 0) return _cmd_run(argc - 1, argv + 1);
    if (strcmp(cmd, "ps")   == 0) return _cmd_ps();
    if (strcmp(cmd, "kill") == 0) return _cmd_kill(argc - 1, argv + 1);

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0) {
        hk_cli_usage();
        return 0;
    }

    HK_ERR("unknown command '%s'", cmd);
    HK_ERR("run 'hollowkernel help' to see available commands");
    return 1;
}