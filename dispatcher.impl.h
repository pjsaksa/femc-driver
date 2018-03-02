/* Femc Driver
 * Copyright (C) 2015-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_DISPATCHER_IMPL_HEADER
#define FEMC_DRIVER_DISPATCHER_IMPL_HEADER

#include "dispatcher.h"

#include <sys/time.h>

typedef struct {
    fdd_service_input *input_handler;
    fdd_service_output *output_handler;
} fd_block_node_t;

// *****

struct fdd_timer_node {
    unsigned int id;
    struct timeval expires;
    fdd_msec_t recurring;

    void *context;
    fdd_notify_func notify;

    struct fdd_timer_node *next;
};

#endif