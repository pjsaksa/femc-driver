// Femc Driver
// Copyright (C) 2015-2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

#include "dispatcher.h"
#include "dispatcher_api.h"

#include <time.h>

typedef struct {
    fdd_service_input* input_handler;
    fdd_service_output* output_handler;
} fd_block_node_t;

// -----

struct fdd_timer_node {
    unsigned int id;
    struct timespec expires;
    fdd_msec_t recurring;

    void* context;
    fdd_notify_func notify;

    struct fdd_timer_node* next;
};

// ------------------------------------------------------------

bool resolve_notify_return(bool notify_ok);
