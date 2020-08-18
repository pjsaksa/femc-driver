// Femc Driver
// Copyright (C) 2013-2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

#include "error_stack.fwd.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// -----

#ifdef FD_DEBUG
# define fde_push_context(A) fde_push_context_(A, __func__)
# define fde_push_debug_message(M) fde_push_message(M)
#else
# define fde_push_context(A) fde_push_context_(A, 0)
# define fde_push_debug_message(M) fde_return_true()

static inline bool fde_return_true(void) { return true; }
#endif

// -----

typedef uint8_t fde_node_type_t;

typedef struct {
    fde_node_type_t type;
    //
    const char* message;
    uint32_t id;
} fde_node_t;

typedef void (*fde_node_callback_func)(const fde_node_t*, void*);

// -----

const fde_node_t* fde_push_context_(uint32_t, const char*);    // context, function

bool fde_push_stdlib_error(const char*, int);           // function, errno
bool fde_push_consistency_failure(const char*);         // message
bool fde_push_consistency_failure_id(uint32_t);         // fde_consistency_XXX
bool fde_push_data_corruption(const char*);             // message
bool fde_push_resource_failure(const char*);            // message
bool fde_push_resource_failure_id(uint32_t);            // fde_resource_XXX
bool fde_push_message(const char*);                     // any message
bool fde_push_node(const fde_node_t*);
//
bool fde_push_http_error(const char*, int);            // status message and code

// -----

uint32_t fde_errors(void);                              // number of errors reported
uint32_t fde_meta_errors(void);                         // number of errors while handling reports

// -----

bool fde_pop_context(uint32_t, const fde_node_t*);      // context, stack_ptr(?)
bool fde_reset_context(uint32_t, const fde_node_t*);    // context, stack_ptr(?)

fde_node_t* fde_get_last_error(uint32_t);               // OR'ed node types (XXX_b ones)
fde_node_t* fde_get_next_error(uint32_t, fde_node_t*);

void fde_for_each_node(uint32_t, fde_node_callback_func, void*);

//

void fde_print_stack(FILE*);
const char* fde_context_id_to_name(fde_context_id_t);

//

static inline bool fde_safe_pop_context(uint32_t context, const fde_node_t* ectx)
{
    return !fde_errors()
        && fde_pop_context(context, ectx);
}

// ----- invariant checks (aka asserts)

#define FDE_ASSERT(EXP, M, RV)                                          \
    do{ if (!(EXP)) { fde_push_data_corruption(M); return RV; } }while(0)

#ifdef FD_DEBUG
# define FDE_ASSERT_DEBUG(EXP, M, RV)                                   \
    do{ if (!(EXP)) { fde_push_data_corruption(M); return RV; } }while(0)
#else
# define FDE_ASSERT_DEBUG(EXP, M, RV) do{}while(0)
#endif

#if defined(FD_DEBUG) && defined(FD_TRACE)
# define FDE_TRACE() do { fprintf(stderr, "--  %s\n", __func__); } while(0)
#else
# define FDE_TRACE() do{}while(0)
#endif
