/*
 * cli.c — command parser and client-side dispatcher
 *
 * Parses argv, builds an hk_request_t, sends it to the daemon,
 * prints the response. No container logic lives here anymore.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hk_cli.h"
#include "hk_daemon.h"
#include "hk_socket.h"
#include "hk_log.h"
#include "hk_ipc.h"
#include "hk_msgqueue.h"

/* ── static handlers  */

static int _cmd_run(int argc, char *argv[])
{
    if (argc < 2)
    {
        HK_ERR("usage: run <name> [priority 0-9] [--rootfs <path>] -- <cmd> [args...]");
        return 1;
    }

    const char *name = argv[1];
    int priority = 5;
    const char *rootfs = NULL;
    int sep_idx = -1;

    /* Find "--" separator first — everything after it is the cmd */
    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--") == 0)
        {
            sep_idx = i;
            break;
        }
    }

    if (sep_idx < 0)
    {
        HK_ERR("missing '--' before the container command");
        return 1;
    }

    /*
     * Scan everything between name and "--" for optional flags
     * and a bare priority number.
     */
    for (int i = 2; i < sep_idx; i++)
    {
        if (strcmp(argv[i], "--rootfs") == 0 && i + 1 < sep_idx)
        {
            rootfs = argv[i + 1];
            i++; /* skip the value we just consumed */
        }
        else
        {
            priority = atoi(argv[i]);
            if (priority < 0 || priority > 9)
            {
                HK_ERR("priority must be 0-9, got '%s'", argv[i]);
                return 1;
            }
        }
    }

    char **cmd = &argv[sep_idx + 1];
    if (cmd[0] == NULL)
    {
        HK_ERR("no command specified after '--'");
        return 1;
    }

    /* ── build request ── */
    hk_request_t req;
    memset(&req, 0, sizeof(req));
    req.type = HK_CMD_RUN;
    req.priority = priority;
    strncpy(req.name, name, sizeof(req.name) - 1);

    if (rootfs != NULL)
        strncpy(req.rootfs, rootfs, sizeof(req.rootfs) - 1);

    int i = 0;
    while (cmd[i] && i < HK_MAX_ARGS)
    {
        strncpy(req.argv[i], cmd[i], HK_ARG_MAX - 1);
        i++;
    }
    req.argc = i;

    /* ── send to daemon ── */
    hk_response_t resp;
    memset(&resp, 0, sizeof(resp));

    if (hk_client_send(&req, &resp) < 0)
        return 1;

    if (resp.status == 0)
        HK_OK("%s", resp.message);
    else
        HK_ERR("%s", resp.message);

    return resp.status;
}

static int _cmd_ps(void)
{
    hk_request_t req;
    memset(&req, 0, sizeof(req));
    req.type = HK_CMD_PS;

    hk_response_t resp;
    memset(&resp, 0, sizeof(resp));

    if (hk_client_send(&req, &resp) < 0)
        return 1;

    /* ps output is pre-formatted by the daemon — just print it */
    printf("%s", resp.message);
    return resp.status;
}

static int _cmd_shm_write(int argc, char *argv[])
{
    if (argc < 3)
    {
        HK_ERR("usage: shm-write <name> <data>");
        return 1;
    }

    const char *name = argv[1];
    const char *data = argv[2];

    /* +1 to include the null terminator so shm-read gets a valid string back */
    if (hk_shm_write(name, data, strlen(data) + 1) < 0)
        return 1;

    return 0;
}

static int _cmd_shm_read(int argc, char *argv[])
{
    if (argc < 2)
    {
        HK_ERR("usage: shm-read <name>");
        return 1;
    }

    const char *name = argv[1];
    char buf[HK_SHM_MAX_SIZE];
    memset(buf, 0, sizeof(buf));

    int n = hk_shm_read(name, buf, sizeof(buf));
    if (n < 0)
        return 1;

    printf("  → %s\n", buf);
    return 0;
}

static int _cmd_mq_send(int argc, char *argv[])
{
    if (argc < 3)
    {
        HK_ERR("usage: mq-send <name> <message> [priority 0-31]");
        return 1;
    }

    /* Queue names must start with '/' per POSIX convention */
    char qname[64];
    snprintf(qname, sizeof(qname), "/%s", argv[1]);

    const char *msg = argv[2];
    unsigned int priority = (argc >= 4) ? (unsigned int)atoi(argv[3]) : 0;

    if (hk_mq_send(qname, msg, strlen(msg) + 1, priority) < 0)
        return 1;

    return 0;
}

static int _cmd_mq_receive(int argc, char *argv[])
{
    if (argc < 2)
    {
        HK_ERR("usage: mq-receive <name>");
        return 1;
    }

    char qname[64];
    snprintf(qname, sizeof(qname), "/%s", argv[1]);

    char buf[HK_MQ_MAX_MSG_SIZE];
    memset(buf, 0, sizeof(buf));

    int n = hk_mq_receive(qname, buf, sizeof(buf));
    if (n < 0)
        return 1;

    printf("  → %s\n", buf);
    return 0;
}

static int _cmd_kill(int argc, char *argv[])
{
    if (argc < 2)
    {
        HK_ERR("usage: kill <id>");
        return 1;
    }

    int id = atoi(argv[1]);
    if (id <= 0)
    {
        HK_ERR("invalid id '%s'", argv[1]);
        return 1;
    }

    hk_request_t req;
    memset(&req, 0, sizeof(req));
    req.type = HK_CMD_KILL;
    req.kill_id = id;

    hk_response_t resp;
    memset(&resp, 0, sizeof(resp));

    if (hk_client_send(&req, &resp) < 0)
        return 1;

    if (resp.status == 0)
        HK_OK("%s", resp.message);
    else
        HK_ERR("%s", resp.message);

    return resp.status;
}

/* ── usage */

void hk_cli_usage(void)
{
    printf("\n");
    printf("  \033[36m┌─────────────────────────────────────┐\033[0m\n");
    printf("  \033[36m│        hollowkernel  runtime        │\033[0m\n");
    printf("  \033[36m└─────────────────────────────────────┘\033[0m\n\n");
    printf("  \033[1mUsage:\033[0m  hollowkernel <command> [options]\n\n");
    printf("  \033[1mCommands:\033[0m\n");
    printf("    daemon\n");
    printf("        Start the background daemon.\n\n");
    printf("    run <name> [priority 0-9] -- <cmd> [args...]\n");
    printf("        Spawn a new isolated container.\n\n");
    printf("    ps\n");
    printf("        List all containers.\n\n");
    printf("    kill <id>\n");
    printf("        Terminate a running container.\n\n");
    printf("    help\n");
    printf("        Show this message.\n\n");
    printf("  \033[2mExamples:\033[0m\n");
    printf("    sudo ./build/hollowkernel daemon\n");
    printf("    sudo ./build/hollowkernel run web 5 -- /bin/sh\n");
    printf("    sudo ./build/hollowkernel ps\n");
    printf("    sudo ./build/hollowkernel kill 1\n\n");
}

/* ── dispatcher */

int hk_cli_dispatch(int argc, char *argv[])
{
    if (argc < 2)
    {
        hk_cli_usage();
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "daemon") == 0)
        return hk_daemon_start();
    if (strcmp(cmd, "run") == 0)
        return _cmd_run(argc - 1, argv + 1);
    if (strcmp(cmd, "ps") == 0)
        return _cmd_ps();
    if (strcmp(cmd, "kill") == 0)
        return _cmd_kill(argc - 1, argv + 1);
    if (strcmp(cmd, "shm-write") == 0)
        return _cmd_shm_write(argc - 1, argv + 1);
    if (strcmp(cmd, "shm-read") == 0)
        return _cmd_shm_read(argc - 1, argv + 1);
    if (strcmp(cmd, "mq-send") == 0)
        return _cmd_mq_send(argc - 1, argv + 1);
    if (strcmp(cmd, "mq-receive") == 0)
        return _cmd_mq_receive(argc - 1, argv + 1);
    if (strcmp(cmd, "help") == 0 ||
        strcmp(cmd, "--help") == 0)
    {
        hk_cli_usage();
        return 0;
    }

    HK_ERR("unknown command '%s'  (try: help)", cmd);
    return 1;
}