/* Femc Driver
 * Copyright (C) 2015-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_DISPATCHER_HEADER
#define FEMC_DRIVER_DISPATCHER_HEADER

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

//

#define FDD_ACTIVE_LOGFILE (fdd_logfile ? fdd_logfile : stderr)

extern FILE* fdd_logfile;

// ------------------------------------------------------------

typedef uint32_t fdd_context_id_t;
typedef uint64_t fdd_msec_t;
typedef bool (*fdd_notify_func)(void*, int);

typedef struct {
    void* context;
    fdd_notify_func notify;
} fdd_service;

typedef struct { fdd_service serv; } fdd_service_input;
typedef struct { fdd_service serv; } fdd_service_output;

// ------------------------------------------------------------

bool fdd_check_input(fdd_service_input*, int);
bool fdd_check_output(fdd_service_output*, int);

bool fdd_add_input(fdd_service_input*, int);
bool fdd_add_output(fdd_service_output*, int);
bool fdd_remove_input(fdd_service_input*, int);
bool fdd_remove_output(fdd_service_output*, int);
bool fdd_remove_input_service(fdd_service_input*);
bool fdd_remove_output_service(fdd_service_output*);

bool fdd_add_timer(fdd_notify_func, void* context, fdd_context_id_t id, fdd_msec_t msec, fdd_msec_t recurring);

// ------------------------------------------------------------

enum { FDD_INFINITE = UINT64_MAX };

bool fdd_main(fdd_msec_t);
void fdd_shutdown(void);

// ------------------------------------------------------------

void fdd_init_service_input(fdd_service_input* service, void* context, fdd_notify_func notify);
void fdd_init_service_output(fdd_service_output* service, void* context, fdd_notify_func notify);

// ------------------------------------------------------------

#define FDD_LOGFILE_NOROTATE    0x1     // ignore signal (SIGHUP) for log rotation

bool fdd_open_logfile(const char* filename, int options);

// ------------------------------------------------------------

typedef bool (*error_resolver_func)(bool notify_ok);

void fdd_set_error_resolver(error_resolver_func new_resolver);

#endif
