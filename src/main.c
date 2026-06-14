#include <stdio.h>
#include <unistd.h>      /* geteuid()  */

#include "hk_container.h"
#include "hk_cli.h"
#include "hk_log.h"

/*
 * main — entry point for hollowkernel
 *
 * Deliberately minimal. Three jobs only:
 *   1. Verify we have root privileges
 *   2. Initialise subsystems
 *   3. Hand off to the CLI dispatcher
 *
 * All real logic lives in the subsystem files.
 */
int main(int argc, char *argv[])
{
    /* 
     * Namespace creation (CLONE_NEWPID, CLONE_NEWNS etc.) requires
     * CAP_SYS_ADMIN. The simplest way to have that is to run as root.
     * We check this before touching anything else so the error is clear.
     */
    if (geteuid() != 0) {
        HK_ERR("hollowkernel must be run as root");
        HK_ERR("try: sudo ./build/hollowkernel <command>");
        return 1;
    }

    /* Boot the container subsystem — zeroes the container table */
    hk_container_init();

    /* Hand off to CLI — it parses argv and calls the right subsystem */
    return hk_cli_dispatch(argc, argv);
}