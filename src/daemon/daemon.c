/*
 * daemon.c — the hollowkernel background daemon
 *
 * Owns the container table. Listens on a Unix domain socket.
 * Serves one client at a time in a simple accept() loop.
 *
 * Architecture:
 *   CLI client  →  connect()  →  send hk_request_t
 *                               ← recv hk_response_t  ← daemon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

/* socket API */
#include <sys/socket.h> /* socket(), bind(), listen(), accept() */
#include <sys/un.h>     /* struct sockaddr_un — Unix sockets    */
#include <sys/stat.h>   /* stat() — check if socket exists      */

#include "hk_daemon.h"
#include "hk_container.h"
#include "hk_socket.h"
#include "hk_log.h"

/* ── internal helpers */

/*
 * Build the socket, bind it to HK_SOCKET_PATH, and start listening.
 * Returns the listening fd on success, -1 on failure.
 */
static int _setup_socket(void)
{
    /*
     * AF_UNIX  = Unix domain socket (local IPC, not network)
     * SOCK_STREAM = reliable, ordered, connection-based (like TCP)
     */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        HK_ERR("socket() failed: %s", strerror(errno));
        return -1;
    }

    /*
     * sockaddr_un is the address struct for Unix sockets.
     * sun_path is just the file path on disk.
     */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, HK_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    /* Remove stale socket file if it exists from a previous run */
    unlink(HK_SOCKET_PATH);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        HK_ERR("bind() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    /*
     * listen() marks this socket as passive — ready to accept.
     * The second arg (5) is the backlog — how many pending
     * connections the kernel will queue before refusing new ones.
     */
    if (listen(fd, 5) < 0)
    {
        HK_ERR("listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/*
 * Handle a single client request.
 * Called once per accepted connection.
 *
 * Reads one hk_request_t, dispatches it, sends back hk_response_t.
 */
static void _handle_client(int client_fd)
{
    hk_request_t req;
    hk_response_t resp;

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    /* Read the full request struct */
    if (hk_recv(client_fd, &req, sizeof(req)) < 0)
    {
        HK_ERR("failed to read request from client");
        return;
    }

    /* Dispatch based on command type */
    switch (req.type)
    {

    case HK_CMD_RUN:
    {
        /*
         * Build a NULL-terminated argv array from the request.
         * req.argv is a 2D array of strings — we need a char*[]
         * for execvp().
         */
        char *argv[HK_MAX_ARGS + 1];
        for (int i = 0; i < req.argc && i < HK_MAX_ARGS; i++)
            argv[i] = req.argv[i];
        argv[req.argc] = NULL;

        int id = hk_container_run(req.name, argv, req.priority);
        if (id > 0)
        {
            resp.status = 0;
            snprintf(resp.message, sizeof(resp.message),
                     "container [%d] '%s' started", id, req.name);
        }
        else
        {
            resp.status = 1;
            snprintf(resp.message, sizeof(resp.message),
                     "failed to start container '%s'", req.name);
        }
        break;
    }

    case HK_CMD_PS:
    {
        /*
         * ps output goes to stdout normally, but here we need
         * to capture it into resp.message to send back to client.
         *
         * We use a trick: redirect stdout to a memstream
         * temporarily, capture the output, then restore stdout.
         */
        hk_container_reap();

        /* Build ps output as a string */
        char *buf = NULL;
        size_t size = 0;
        FILE *mem = open_memstream(&buf, &size);

        if (mem)
        {
            /* Temporarily redirect stdout */
            FILE *old_stdout = stdout;
            stdout = mem;
            hk_container_ps();
            stdout = old_stdout;
            fclose(mem);

            strncpy(resp.message, buf, sizeof(resp.message) - 1);
            free(buf);
        }
        else
        {
            strncpy(resp.message, "(ps failed)",
                    sizeof(resp.message) - 1);
        }

        resp.status = 0;
        break;
    }

    case HK_CMD_KILL:
    {
        hk_rc_t rc = hk_container_kill(req.kill_id);
        if (rc == HK_OK)
        {
            resp.status = 0;
            snprintf(resp.message, sizeof(resp.message),
                     "container [%d] killed", req.kill_id);
        }
        else
        {
            resp.status = 1;
            snprintf(resp.message, sizeof(resp.message),
                     "failed to kill container [%d]", req.kill_id);
        }
        break;
    }

    default:
        resp.status = 1;
        snprintf(resp.message, sizeof(resp.message),
                 "unknown command type %d", req.type);
        break;
    }

    /* Send the response back */
    hk_send(client_fd, &resp, sizeof(resp));
}

/*
 * _daemon_loop — the infinite accept() loop
 *
 * This runs forever in the child process after fork().
 * Each iteration: accept a client, handle it, close it.
 */
static void _daemon_loop(int listen_fd)
{
    HK_OK("daemon listening on %s", HK_SOCKET_PATH);
    HK_INFO("waiting for commands...");

    while (1)
    {
        /*
         * accept() blocks here until a client connects.
         * Returns a NEW fd specific to that client connection.
         * The original listen_fd stays open for future clients.
         */
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue; /* interrupted by signal, retry */
            HK_ERR("accept() failed: %s", strerror(errno));
            continue;
        }

        _handle_client(client_fd);
        close(client_fd);
    }
}

/* ── public API */

int hk_daemon_start(void)
{
    /* Check if daemon is already running */
    struct stat st;
    if (stat(HK_SOCKET_PATH, &st) == 0)
    {
        HK_WARN("socket already exists at %s", HK_SOCKET_PATH);
        HK_WARN("daemon may already be running");
        HK_WARN("if not, remove it with: rm %s", HK_SOCKET_PATH);
        return 1;
    }

    int listen_fd = _setup_socket();
    if (listen_fd < 0)
        return -1;

    /*
     * Fork — split into two processes.
     *
     * fork() returns:
     *   >0  we are the PARENT — got the child's pid
     *    0  we are the CHILD
     *   -1  error
     */
    pid_t pid = fork();

    if (pid < 0)
    {
        HK_ERR("fork() failed: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    if (pid > 0)
    {
        /*
         * PARENT — print success and exit.
         * Terminal gets control back immediately.
         * The child (daemon) keeps running in background.
         */
        HK_OK("daemon started  (pid=%d)", pid);
        HK_INFO("socket: %s", HK_SOCKET_PATH);
        close(listen_fd);
        return 0;
    }

    /*
     * CHILD — we are the daemon now.
     * Detach from the terminal session so we don't die
     * when the user closes their terminal.
     */
    setsid();

    /* Initialise the container subsystem inside the daemon */
    hk_container_init();

    /* Enter the infinite accept loop — never returns */
    _daemon_loop(listen_fd);

    return 0; /* unreachable */
}

int hk_client_send(const hk_request_t *req, hk_response_t *resp)
{
    /*
     * Create a client socket and connect to the daemon.
     * Same address family and type as the server.
     */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        HK_ERR("socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, HK_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        HK_ERR("cannot connect to daemon at %s", HK_SOCKET_PATH);
        HK_ERR("is the daemon running? try: sudo ./build/hollowkernel daemon");
        close(fd);
        return -1;
    }

    /* Send request, receive response */
    if (hk_send(fd, req, sizeof(*req)) < 0)
    {
        close(fd);
        return -1;
    }

    if (hk_recv(fd, resp, sizeof(*resp)) < 0)
    {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}