// Femc Driver
// Copyright (C) 2015-2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

//

#define FDD_ACTIVE_LOGFILE (fdd_logfile ? fdd_logfile : stderr)

extern FILE* fdd_logfile;

// ------------------------------------------------------------

typedef int fdd_context_id_t;
typedef uint64_t fdd_msec_t;
typedef bool (*fdd_notify_func)(void*, int);

typedef struct {
    void* context;
    fdd_notify_func notify;
} fdd_service;

typedef struct { fdd_service serv; } fdd_service_input;
typedef struct { fdd_service serv; } fdd_service_output;

// ------------------------------------------------------------

bool fdd_add_input(int fd, fdd_service_input* service);
bool fdd_add_output(int fd, fdd_service_output* service);
bool fdd_remove_input(int fd);
bool fdd_remove_output(int fd);

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
