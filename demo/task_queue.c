// Femc Driver
// Copyright (C) 2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#include "../dispatcher.h"
#include "../error_stack.h"
#include "../generic.h"
#include "../task_queue.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    fdu_task_queue_t* queue;

    int end_queue;

    int ticks;
    int total_ticks;
} tick_context_t;

static bool tick(void* void_context,
                 int UNUSED(dummy))
{
    const fde_node_t* ectx = fde_push_context(fd_demo_context_task_queue);
    if (!ectx) return false;
    //

    tick_context_t* context = (tick_context_t*)void_context;

    if (context == NULL) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    //

    ++context->ticks;

    if (context->queue != NULL
        && context->ticks == context->end_queue)
    {
        printf("freeing queue\n");

        fdu_free_task_queue(context->queue);
        context->queue = NULL;
    }

    if (context->ticks >= context->total_ticks)
    {
        fde_push_consistency_failure_id(fde_consistency_kill_recurring_timer);
        return false;
    }
    else {
        return fde_pop_context(fd_demo_context_task_queue, ectx);
    }
}

//

typedef struct {
    fdu_task_queue_t* queue;
    int task_num;

    int round;
    int total_rounds;
} task_context_t;

static bool task_n(void* void_context,
                   int task_id)
{
    const fde_node_t* ectx = fde_push_context(fd_demo_context_task_queue);
    if (!ectx) return false;
    //

    task_context_t* context = (task_context_t*)void_context;

    if (context == NULL) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    ++context->round;

    {
        static uint64_t last_msec = 0;
        uint64_t now_msec;

        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &now) < 0) {
            fde_push_context(fd_demo_context_task_queue);
            fde_push_stdlib_error("clock_gettime(CLOCK_MONOTONIC_RAW)", errno);
            return false;
        }
        now_msec = (now.tv_sec*1000) + (now.tv_nsec/1000000);

        printf("<%02d> ", task_id);

        if (last_msec != 0) {
            printf("@%03lu ", now_msec - last_msec);
        }
        else {
            printf("@--- ");
        }

        last_msec = now_msec;

        printf(": task %d, %d/%d\n", context->task_num, context->round, context->total_rounds);
    }

    if (context->round < context->total_rounds
        && !fdu_add_task(context->queue, &task_n, context))
    {
        return false;
    }

    return fdu_complete_task(context->queue,
                             task_id)
        && fde_pop_context(fd_demo_context_task_queue, ectx);
}

int main(void)
{
    fdu_task_queue_t* global_queue = fdu_new_task_queue(1, 100);

    tick_context_t tick_context = { global_queue, 2, 0, 5 };

    task_context_t context[5] = {
        { global_queue, 1, 0, 1 },
        { global_queue, 2, 0, 2 },
        { global_queue, 3, 0, 4 },
        { global_queue, 4, 0, 8 },
        { global_queue, 5, 0, 8 },
    };

    return (fdu_add_task(global_queue, &task_n, &context[0])
            && fdu_add_task(global_queue, &task_n, &context[1])
            && fdu_add_task(global_queue, &task_n, &context[2])
            && fdu_add_task(global_queue, &task_n, &context[3])
            && fdu_add_task(global_queue, &task_n, &context[4])
            //
            && fdd_add_timer(tick, &tick_context, 0, 1000, 1000)
            //
            && fdd_main(FDD_INFINITE))
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
