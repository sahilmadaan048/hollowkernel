#ifndef HK_CLI_H
#define HK_CLI_H

/*
 * hk_cli.h — public API for the CLI module
 *
 * The CLI is the layer between the user typing commands and the
 * subsystems (container manager, scheduler etc.) doing the work.
 *
 * Only two functions are public. Everything else in cli.c is
 * static — internal implementation detail, not your concern.
 */

/*
 * Parse argc/argv and dispatch to the right subsystem.
 *
 * This is called directly from main() with the raw argv.
 * Returns 0 on success, 1 on error or bad usage.
 *
 * Supported commands (Phase 1):
 *   run  <name> [priority] -- <cmd> [args...]
 *   ps
 *   kill <id>
 *   help
 */
int hk_cli_dispatch(int argc, char *argv[]);

/*
 * Print usage information to stdout.
 * Called when user passes no args or types 'help'.
 */
void hk_cli_usage(void);

#endif /* HK_CLI_H */