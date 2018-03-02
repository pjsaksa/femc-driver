/* Femc Driver - babysitter demo
 * Copyright (C) 2015-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "generic.h"
#include "error_stack.h"
#include "utils.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/stat.h>
#include <sys/types.h>

static void handle_pidfile(char *program)
{
    char filename[FILENAME_MAX];

    mkdir("pid", 0777); // ignore errors

    snprintf(filename, FILENAME_MAX,
             "pid/%s.pid",
             program);
    filename[FILENAME_MAX-1] =0;

    if (!fdu_pidfile(filename, 0))
    {
        fde_node_t *err =0;

        if ((err =fde_get_last_error(fde_node_stdlib_error_b))
            && err->message
            && strcmp(err->message, "fcntl") == 0
            && err->id == EAGAIN
            )
        {
            fprintf(FDD_ACTIVE_LOGFILE, "%s is already running.\n", program);
        }
        else {
            fde_print_stack(FDD_ACTIVE_LOGFILE);
        }

        exit(1);
    }
}

int main(int UNUSED(argc), char *argv[])
{
    handle_pidfile(argv[0]);

    for (;;)
        pause();

    return 0;
}
