// Femc Driver
// Copyright (C) 2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#include "task_queue.h"

#include "dispatcher.impl.h"
#include "error_stack.h"
#include "generic.h"

#include <stdint.h>
#include <stdlib.h>

enum {
    TaskAllocationChunk = 16,
};

//

typedef struct task_s task_t;

struct task_s {
    fdd_notify_func handler;
    void* handler_context;

    task_t* next_task;
};

struct fdu_task_queue_s {
    task_t* first_task;
    task_t* last_task;

    task_id_t next_task_id;
    struct timespec next_run_time;

    fdd_timer_handle_t timer_handle;
    fdd_msec_t task_interval;
};

// functions

static task_t* f_misplaced_tasks = NULL;

static void zero_task(task_t* task)
{
    task->handler         = NULL;
    task->handler_context = NULL;
    task->next_task       = NULL;
}

static void misplace_task(task_t* task)
{
    zero_task(task);

    task->next_task = f_misplaced_tasks;
    f_misplaced_tasks = task;
}

static task_t* obtain_task(void)
{
    if (f_misplaced_tasks == NULL)
    {
        task_t* chunk = malloc(sizeof(task_t) * TaskAllocationChunk);

        if (chunk == NULL) {
            fde_push_resource_failure_id(fde_resource_memory_allocation);
            return NULL;
        }

        for (int i = 0;
             i < TaskAllocationChunk;
             ++i)
        {
            misplace_task(chunk + i);
        }
    }

    //

    task_t* task = f_misplaced_tasks;
    f_misplaced_tasks = f_misplaced_tasks->next_task;

    zero_task(task);

    return task;
}

fdu_task_queue_t* fdu_new_task_queue(fdd_timer_handle_t timer_handle,
                                     fdd_msec_t task_interval)
{
    fdu_task_queue_t* queue = malloc(sizeof(fdu_task_queue_t));

    if (queue == NULL) {
        fde_push_context(fdu_context_task_queue);
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return NULL;
    }

    //

    queue->first_task    = NULL;
    queue->last_task     = NULL;
    queue->next_task_id  = 0;
    queue->timer_handle  = timer_handle;
    queue->task_interval = task_interval;

    queue->next_run_time.tv_sec  = 0;
    queue->next_run_time.tv_nsec = 0;

    return queue;
}

void fdu_free_task_queue(fdu_task_queue_t* queue)
{
    if (queue == NULL) {
        fde_push_context(fdu_context_task_queue);
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return;
    }

    if (queue->timer_handle != 0) {
        fdd_cancel_timer(queue->timer_handle);
    }

    free(queue);
}

static bool launch_next_task(void* void_queue,
                             int UNUSED(dummy))
{
    const fde_node_t* ectx = fde_push_context(fdu_context_task_queue);
    if (!ectx) return false;
    //
    fdu_task_queue_t* queue = (fdu_task_queue_t*)void_queue;
    if (queue == NULL) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    task_t* task = queue->first_task;

    return task->handler(task->handler_context,
                         ++queue->next_task_id)
        && fde_pop_context(fdu_context_task_queue, ectx);
}

static bool setup_next_task_launch(fdu_task_queue_t* queue)
{
    if (queue->first_task == NULL) {
        return true;
    }

    fdd_msec_t msec_to_run = 0;

    if (!expiration_msec(&queue->next_run_time, &msec_to_run)) {
        // error pushed by expiration_msec()
        return false;
    }

    //

    if (msec_to_run > 0) {
        return fdd_add_timer_handle(launch_next_task,
                                    queue, 0,
                                    msec_to_run, 0,
                                    queue->timer_handle);
    }
    else {
        return launch_next_task(queue, 0);
    }
}

bool fdu_add_task(fdu_task_queue_t* queue,
                  fdd_notify_func handler,
                  void* handler_context)
{
    const fde_node_t* ectx = fde_push_context(fdu_context_task_queue);
    if (ectx == NULL) return false;
    //
    if (queue == NULL
        || handler == NULL)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    task_t* task = obtain_task();

    if (task == NULL) {
        // error pushed by obtain_task()
        return false;
    }

    task->handler         = handler;
    task->handler_context = handler_context;

    //

    if (queue->first_task == NULL)
    {
        queue->first_task = task;
        queue->last_task  = task;

        if (!setup_next_task_launch(queue)) {
            return false;
        }
    }
    else {
        queue->last_task->next_task = task;
        queue->last_task = task;
    }

    //

    return fde_pop_context(fdu_context_task_queue, ectx);
}

bool fdu_complete_task(fdu_task_queue_t* queue,
                       task_id_t task_id)
{
    const fde_node_t* ectx = fde_push_context(fdu_context_task_queue);
    if (!ectx) return false;
    //

    if (queue == NULL
        || task_id != queue->next_task_id)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    if (queue->first_task == NULL) {
        fde_push_consistency_failure_id(fde_consistency_task_queue_corrupted);
        return false;
    }

    //

    task_t* task = queue->first_task;
    queue->first_task = queue->first_task->next_task;
    misplace_task(task);
    if (queue->first_task == NULL) {
        queue->last_task = NULL;
    }

    //

    if (!get_expiration_time(&queue->next_run_time, queue->task_interval)) {
        // error pushed by get_expiration_time()
        return false;
    }

    //

    return setup_next_task_launch(queue)
        && fde_pop_context(fdu_context_task_queue, ectx);
}
