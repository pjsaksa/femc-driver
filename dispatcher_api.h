/* Femc Driver
 * Copyright (C) 2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_DISPATCHER_API_HEADER
#define FEMC_DRIVER_DISPATCHER_API_HEADER

#include "dispatcher.h"

typedef bool (*fdd_init_f)(void);
typedef bool (*fdd_poll_f)(fdd_msec_t msec);
typedef bool (*fdd_empty_f)(void);

typedef bool (*fdd_add_input_f)(int fd, fdd_service_input* service);
typedef bool (*fdd_add_output_f)(int fd, fdd_service_output* service);
typedef bool (*fdd_remove_input_f)(int fd);
typedef bool (*fdd_remove_output_f)(int fd);

//

typedef struct {
    fdd_init_f  init;
    fdd_poll_f  poll;
    fdd_empty_f empty;
    //
    fdd_add_input_f     add_input;
    fdd_add_output_f    add_output;
    fdd_remove_input_f  remove_input;
    fdd_remove_output_f remove_output;
} fdd_impl_api_t;

//

extern const fdd_impl_api_t fdd_impl_select;
extern const fdd_impl_api_t fdd_impl_zmq;

//

extern const fdd_impl_api_t* fdd_impl_api;

#endif
