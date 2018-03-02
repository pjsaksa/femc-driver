/* Femc Driver - babysitter demo
 * Copyright (C) 2017-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "babysitter.h"

#include "dispatcher.h"
#include "utils.h"
#include "error_stack.h"
#include "generic.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

static void file_entries_changed(const babysitter_public_t *user)
{
    putchar('=');

    for (uint32_t i =0;
         i < user->num_of_entries;
         ++i)
    {
        if (user->entries[i].pid <= 0)
            continue;

        printf(" %s(%d)", user->entries[i].name, user->entries[i].pid);
    }

    putchar('\n');
}

static void program_alive(const babysitter_public_t *user, const char *name, pid_t pid)
{
    printf("+ %s(%d)\n", name, pid);
    file_entries_changed(user);
}

static void program_dead(const babysitter_public_t *user, const char *name, pid_t pid)
{
    printf("- %s(%d)\n", name, pid);
    file_entries_changed(user);
}

// *********************************************************

static babysitter_public_t *master_babysitter = 0;

//

static void terminate_programs(int signum)
{
    for (uint32_t i =0;
         i < master_babysitter->num_of_entries;
         ++i)
    {
        if (master_babysitter->entries[i].pid <= 0)
            continue;

        kill(master_babysitter->entries[i].pid, signum);
    }
}

static void terminate_programs_and_exit(int UNUSED(signum))
{
    terminate_programs(SIGTERM);
    exit(0);
}

static bool initialize_signals(void)
{
    if (signal(SIGTERM, terminate_programs) != SIG_ERR
        && signal(SIGQUIT, terminate_programs_and_exit) != SIG_ERR)
    {
        return true;
    }

    fde_push_stdlib_error("signal", errno);
    return false;
}

// *********************************************************

int main(void)
{
    mkdir("pid", 0777); // ignore errors

    if (initialize_signals()
        && fdu_safe_chdir("pid")
        && fdu_pidfile("../babysitter.pid", 0)
        && fdd_open_logfile("../babysitter.log", 0)
        //
        && (master_babysitter =new_babysitter_service(&program_alive,
                                                      &program_dead))
        && fdd_main(FDD_INFINITE))
    {
        return EXIT_SUCCESS;
    }
    else {
        fde_print_stack(FDD_ACTIVE_LOGFILE);
        return EXIT_FAILURE;
    }
}
