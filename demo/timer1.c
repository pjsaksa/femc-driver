/* Femc Driver
 * Copyright (C) 2015-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "timer1.h"

#include "generic.h"
#include "dispatcher.h"
#include "error_stack.h"

#include <stdio.h>
#include <errno.h>

enum { this_error_context = fd_demo_context_timer1 };

//

static bool print(const char* message)
{
    if (puts(message) == EOF) {
        fde_push_stdlib_error("puts", errno);
        return false;
    }

    if (fflush(stdout) != 0) {
        fde_push_stdlib_error("fflush", errno);
        return false;
    }

    return true;
}

static bool running = true;

static bool exit_sequence_2(void* UNUSED(context), int UNUSED(id))
{
    running = false;
    return true;
}

static bool exit_sequence_1(void* UNUSED(context), int UNUSED(id))
{
    const fde_node_t* ectx;

    return (ectx =fde_push_context(this_error_context))
        && print("BAM")
        && fdd_add_timer(&exit_sequence_2, 0, 0, 3000, 0)
        && fde_pop_context(this_error_context, ectx);
}

static bool timer1_timer(void* message_v, int UNUSED(id))
{
    const char* message = (const char*) message_v;

    if (!running) {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_kill_recurring_timer);
        return false;
    }

    const fde_node_t* ectx;

    return (ectx =fde_push_context(this_error_context))
        && print(message)
        && fde_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

bool timer1_start(void)
{
    const fde_node_t* ectx;

    return (ectx =fde_push_context(this_error_context))
        && fprintf(FDD_ACTIVE_LOGFILE, "starting timer1\n") > 0
        && fdd_add_timer(&timer1_timer, " 1/2 sec", 0, 500, 500)
        && fdd_add_timer(&timer1_timer, " 2   sec", 0, 2000, 2000)
        && fdd_add_timer(&timer1_timer, "10/3 sec", 0, 3333, 3333)
        && fdd_add_timer(exit_sequence_1, 0, 0, 10001, 0)
        && fde_pop_context(this_error_context, ectx);
}
