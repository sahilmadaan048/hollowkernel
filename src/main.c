#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hk_cli.h"
#include "hk_log.h"

int main(int argc, char *argv[])
{
    /*
     * Only the daemon command needs root — it calls clone() with
     * namespace flags. Client commands (run/ps/kill) just talk to
     * the daemon over a socket and need no special privileges.
     */
    if (argc >= 2 && strcmp(argv[1], "daemon") == 0) {
        if (geteuid() != 0) {
            HK_ERR("daemon must be started as root (needs CAP_SYS_ADMIN)");
            HK_ERR("try: sudo ./build/hollowkernel daemon");
            return 1;
        }
    }

    return hk_cli_dispatch(argc, argv);
}