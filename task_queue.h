// Femc Driver
// Copyright (C) 2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

#include "dispatcher.h"

typedef int task_id_t;
typedef struct fdu_task_queue_s fdu_task_queue_t;

fdu_task_queue_t* fdu_new_task_queue(fdd_timer_handle_t timer_handle,
                                     fdd_msec_t task_interval);
void fdu_free_task_queue(fdu_task_queue_t* queue);

bool fdu_add_task(fdu_task_queue_t* queue,
                  fdd_notify_func handler,
                  void* handler_context);
bool fdu_complete_task(fdu_task_queue_t* queue,
                       task_id_t task_id);
