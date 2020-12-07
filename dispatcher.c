/* Femc Driver
 * Copyright (C) 2015-2020 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "dispatcher.impl.h"
#include "generic.h"
#include "error_stack.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

enum { this_error_context = fdd_context_main };

static const fde_node_t* main_error_context = 0;

// ------------------------------------------------------------

FILE* fdd_logfile = 0;

// ------------------------------------------------------------

static bool default_resolve_error(bool UNUSED(notify_ok))
{
#ifdef FD_DEBUG

    // DEBUG: print error stack and halt

    fde_print_stack(FDD_ACTIVE_LOGFILE);
    return false;
#else

    // PRODUCTION: go on and hope that everything works out

    return true;
#endif
}

static error_resolver_func error_resolver = &default_resolve_error;

void fdd_set_error_resolver(error_resolver_func new_resolver)
{
    if (new_resolver)
        error_resolver = new_resolver;
    else
        error_resolver = &default_resolve_error;
}

// -----

bool resolve_notify_return(bool notify_ok)
{
    if (notify_ok
        && !fde_errors()
        && fde_reset_context(this_error_context, main_error_context))
    {
        return true;
    }

    return error_resolver(notify_ok)
        && fde_reset_context(this_error_context, main_error_context);
}

// ------------------------------------------------------------

static char fdd_logfile_filename[FILENAME_MAX];
static volatile bool fdd_logfile_changed = false;

static void fdd_logfile_notify(int UNUSED(signum)) // signal handler
{
    fdd_logfile_changed = true;
}

static bool fdd_logfile_reopen(void)
{
    fdd_logfile = (fdd_logfile
                   ? freopen(fdd_logfile_filename, "a", fdd_logfile)
                   : fopen(fdd_logfile_filename, "a"));

    if (!fdd_logfile) {
        fde_push_stdlib_error("fopen/freopen(fdd_logfile_filename)", errno);
        return false;
    }
    setbuf(fdd_logfile, 0);

    //

    fdd_logfile_changed = false;
    return true;
}

// ------------------------------------------------------------

bool get_expiration_time(struct timespec* tv, fdd_msec_t msec)
{
#ifdef FD_DEBUG
    if (!tv) {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
#endif

    if (clock_gettime(CLOCK_MONOTONIC_RAW, tv) < 0) {
        fde_push_context(this_error_context);
        fde_push_stdlib_error("clock_gettime(CLOCK_MONOTONIC_RAW)", errno);
        return false;
    }

    if (!msec) return true;

    tv->tv_sec  += msec/1000;
    tv->tv_nsec += (msec%1000)*1000000;

    while (tv->tv_nsec >= 1000000000) {
        ++tv->tv_sec;
        tv->tv_nsec -= 1000000000;
    }

    return true;
}

int expiration_compare(struct timespec* a, struct timespec* b)
{
    return (a->tv_sec != b->tv_sec)
        ? (a->tv_sec - b->tv_sec)
        : (a->tv_nsec - b->tv_nsec);
}

bool expiration_msec(struct timespec* tv, fdd_msec_t* msec)
{
#ifdef FD_DEBUG
    if (!tv || !msec) {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
#endif

    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &now) < 0) {
        fde_push_context(this_error_context);
        fde_push_stdlib_error("clock_gettime(CLOCK_MONOTONIC_RAW)", errno);
        return false;
    }

    if (expiration_compare(tv, &now) <= 0) {
        *msec = 0;
        return true;
    }

    *msec = ((tv->tv_sec - now.tv_sec) * 1000
             + (tv->tv_nsec - now.tv_nsec) / 1000000);

    return true;
}

bool add_expiration_msec(struct timespec* tv, fdd_msec_t msec)
{
#ifdef FD_DEBUG
    if (!tv || !msec) {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
#endif

    const fdd_msec_t sec  = (fdd_msec_t)tv->tv_sec  +  msec/1000;
    const fdd_msec_t nsec = (fdd_msec_t)tv->tv_nsec + (msec%1000)*1000000;

    if (nsec > 1000000000u) {
        tv->tv_sec  = sec +1;
        tv->tv_nsec = nsec -1000000000;
    }
    else {
        tv->tv_sec  = sec;
        tv->tv_nsec = nsec;
    }

    return true;
}

// ------------------------------------------------------------

enum { size_of_timer_alloc_block = 32 };

//

static struct fdd_timer_node* free_timer_nodes = 0;

static void timer_free_node(struct fdd_timer_node* tbd)
{
    tbd->next = free_timer_nodes;
    free_timer_nodes = tbd;
}

static struct fdd_timer_node* timer_alloc_node(fdd_notify_func notify,
                                               void* context,
                                               fdd_context_id_t id,
                                               fdd_msec_t msec,
                                               fdd_msec_t recurring,
                                               fdd_timer_handle_t handle)
{
    if (!free_timer_nodes)
    {
        struct fdd_timer_node* new_nodes = malloc(size_of_timer_alloc_block * sizeof(struct fdd_timer_node));

        if (new_nodes) {
            for (unsigned int i = 0;
                 i < size_of_timer_alloc_block;
                 ++i)
            {
                timer_free_node(&new_nodes[i]);
            }
        }
        else {
            fprintf(FDD_ACTIVE_LOGFILE, "info: block allocation failed, trying to allocate 1 node\n");

            if (!(free_timer_nodes =malloc(sizeof(struct fdd_timer_node))))
                return 0;
            free_timer_nodes->next = 0;
        }
    }

    //

    struct fdd_timer_node* node = free_timer_nodes;
    free_timer_nodes = free_timer_nodes->next;

    //

    if (!get_expiration_time(&node->expires, msec)) {
        timer_free_node(node);
        return NULL;
    }

    node->recurring = recurring;
    node->notify    = notify;
    node->context   = context;
    node->id        = id;
    node->handle    = handle;
    node->next      = 0;

    return node;
}

// ------------------------------------------------------------

static struct fdd_timer_node* f_dispatcher_timers = 0;

static void fdd_add_timer_node(struct fdd_timer_node* new_node)
{
    // check if the new node should be inserted as first

    if (!f_dispatcher_timers
        || expiration_compare(&new_node->expires, &f_dispatcher_timers->expires) < 0)
    {
        new_node->next = f_dispatcher_timers;
        f_dispatcher_timers = new_node;
        return;
    }

    // iterate f_dispatcher_timers until correct place for insertion is found

    {
        struct fdd_timer_node* ptr = f_dispatcher_timers;
        while (ptr->next) {
            if (expiration_compare(&new_node->expires, &ptr->next->expires) < 0)
            {
                new_node->next = ptr->next;
                ptr->next = new_node;
                return;
            }

            ptr = ptr->next;
        }

        // add to last

        new_node->next = 0;
        ptr->next = new_node;
    }
}

bool fdd_add_timer(fdd_notify_func notify,
                   void* context,
                   fdd_context_id_t id,
                   fdd_msec_t msec,
                   fdd_msec_t recurring)
{
    return fdd_add_timer_handle(notify,
                                context,
                                id,
                                msec,
                                recurring,
                                0);
}

bool fdd_add_timer_handle(fdd_notify_func notify,
                          void* context,
                          fdd_context_id_t id,
                          fdd_msec_t msec,
                          fdd_msec_t recurring,
                          fdd_timer_handle_t handle)
{
#ifdef FD_DEBUG
    if (notify == NULL) {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
#endif

    // alloc and init timer node

    struct fdd_timer_node* new_node = timer_alloc_node(notify, context, id, msec, recurring, handle);

    if (new_node == NULL) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return false;
    }

    fdd_add_timer_node(new_node);
    return true;
}

void fdd_cancel_timer(fdd_timer_handle_t handle)
{
    if (handle == 0) {
        return;
    }

    while (f_dispatcher_timers != NULL
           && f_dispatcher_timers->handle == handle)
    {
        struct fdd_timer_node* node = f_dispatcher_timers;
        f_dispatcher_timers = f_dispatcher_timers->next;
        timer_free_node(node);
    }

    if (f_dispatcher_timers == NULL) {
        return;
    }

    struct fdd_timer_node* node = f_dispatcher_timers;

    while (node->next != NULL)
    {
        if (node->next->handle == handle)
        {
            struct fdd_timer_node* old = node->next;
            node->next = node->next->next;
            timer_free_node(old);
        }
        else {
            node = node->next;
        }
    }
}

// ------------------------------------------------------------

static bool running = true;

bool fdd_main(fdd_msec_t max_msec)
{
    if (fde_errors())
        return false;
    if (!(main_error_context =fde_push_context(this_error_context)))
        return false;

    //

    static bool initialized = false;

    if (!initialized) {
        if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
            fde_push_stdlib_error("signal(SIGPIPE)", errno);
            return false;
        }

        fdd_impl_api->init();

        initialized = true;
    }

    //

    unsigned int timers_handled = 0;
    struct timespec max_expires;

    if (max_msec > 0 && max_msec < FDD_INFINITE)
        if (!get_expiration_time(&max_expires, max_msec))
            return false;

    //

    running = true;

    while (running
           && (f_dispatcher_timers
               || !fdd_impl_api->empty()))
    {
        fdd_msec_t msec = FDD_INFINITE;

        if (f_dispatcher_timers)
        {
            if (!expiration_msec(&f_dispatcher_timers->expires, &msec))
                return false;

            if (!msec)
            {
                struct fdd_timer_node* tmr = f_dispatcher_timers;
                f_dispatcher_timers = f_dispatcher_timers->next;

                bool timer_ok = tmr->notify(tmr->context, tmr->id);

                if (tmr->recurring)
                {
                    fde_node_t* err = 0;

                    if (!timer_ok
                        && fde_errors() == 1
                        && (err =fde_get_last_error(fde_node_consistency_failure_b))
                        && !err->message
                        && err->id == fde_consistency_kill_recurring_timer)
                    {
                        if (fde_reset_context(this_error_context, main_error_context))
                            timer_ok = true;
                    }
                    else if (add_expiration_msec(&tmr->expires, tmr->recurring))
                    {
                        {
                            // If more than one occurrence of the timer is already pending, merge them into one.

                            struct timespec next_recurring = tmr->expires;
                            fdd_msec_t next_recurring_msec;

                            while (add_expiration_msec(&next_recurring, tmr->recurring)
                                   && expiration_msec(&next_recurring, &next_recurring_msec)
                                   && next_recurring_msec <= 0)
                            {
                                tmr->expires = next_recurring;
                            }
                        }

                        fdd_add_timer_node(tmr);
                        tmr = 0;
                    }
                }

                if (tmr) timer_free_node(tmr);

                if (!resolve_notify_return(timer_ok))
                    return false;

                ++timers_handled;
                continue;
            }
        }

        if (max_msec < FDD_INFINITE)
        {
            if (timers_handled
                && max_msec > 0)
            {
                if (!expiration_msec(&max_expires, &max_msec))
                    return false;
            }

            if (msec > max_msec)
                msec = max_msec;
        }

        if (!fdd_impl_api->poll(msec))
            return false;

        if (max_msec > 0 && max_msec < FDD_INFINITE) {
            if (!expiration_msec(&max_expires, &max_msec))
                return false;
        }

        if (!max_msec)
            break;

        if (fdd_logfile_changed) {
            if (!fdd_logfile_reopen()) {
                fprintf(FDD_ACTIVE_LOGFILE, "dispatcher: reopening log failed\n");
                return false;
            }
        }

        timers_handled = 0;
    }

    return fde_safe_pop_context(this_error_context, main_error_context);
}

void fdd_shutdown(void)
{
    running = false;
}

// ------------------------------------------------------------

void fdd_init_service_input(fdd_service_input* service, void* context, fdd_notify_func notify)
{
    service->serv.context = context;
    service->serv.notify = notify;
}

void fdd_init_service_output(fdd_service_output* service, void* context, fdd_notify_func notify)
{
    service->serv.context = context;
    service->serv.notify = notify;
}

// ------------------------------------------------------------

bool fdd_open_logfile(const char* filename, int options)
{
    const fde_node_t* ectx = 0;

    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    //

#ifdef FD_DEBUG
    if (!filename
        || !*filename
        || !memchr(filename, 0, FILENAME_MAX)
        || (options & ~(FDD_LOGFILE_NOROTATE)))  // clear all legal option bits
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
#endif

    //

    strncpy(fdd_logfile_filename, filename, FILENAME_MAX);
    fdd_logfile_filename[FILENAME_MAX-1] = 0;

    //

    fdd_logfile = (fdd_logfile
                   ? freopen(fdd_logfile_filename, "a", fdd_logfile)
                   : fopen(fdd_logfile_filename, "a"));

    if (!fdd_logfile) {
        fde_push_stdlib_error("fopen/freopen(fdd_logfile_filename)", errno);
        return false;
    }
    setbuf(fdd_logfile, 0);

    //

    if (!(options & FDD_LOGFILE_NOROTATE))
    {
        if (signal(SIGHUP, &fdd_logfile_notify) == SIG_ERR) {
            fde_push_stdlib_error("signal(SIGHUP)", errno);
            return false;
        }
    }

    return fde_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

bool fdd_add_input(int fd, fdd_service_input* service)
{
    return fdd_impl_api->add_input(fd, service);
}

bool fdd_add_output(int fd, fdd_service_output* service)
{
    return fdd_impl_api->add_output(fd, service);
}

bool fdd_remove_input(int fd)
{
    return fdd_impl_api->remove_input(fd);
}

bool fdd_remove_output(int fd)
{
    return fdd_impl_api->remove_output(fd);
}

// ------------------------------------------------------------

extern const fdd_impl_api_t* fdd_impl_api WEAK_LINKAGE;
const fdd_impl_api_t* fdd_impl_api = &fdd_impl_select;
