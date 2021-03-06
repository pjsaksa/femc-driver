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
    struct timespec expires;
    fdd_msec_t recurring;

    fdd_notify_func notify;
    void* context;
    unsigned int id;

    uint32_t handle;

    struct fdd_timer_node* next;
};

// ------------------------------------------------------------

bool resolve_notify_return(bool notify_ok);

// ------------------------------------------------------------

// current time + msec -> tv
bool get_expiration_time(struct timespec* tv, fdd_msec_t msec);

// (a > b) ? (return > 0)
int expiration_compare(struct timespec* a, struct timespec* b);

// tv - current time -> msec
bool expiration_msec(struct timespec* tv, fdd_msec_t* msec);

// tv += msec
bool add_expiration_msec(struct timespec* tv, fdd_msec_t msec);
