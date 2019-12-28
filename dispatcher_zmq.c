/* Femc Driver
 * Copyright (C) 2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "dispatcher.impl.h"
#include "error_stack.h"

#include "zmq.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { this_error_context = fdd_context_zmq };

enum {
    EntryAllocationBlock = 16,
};

// ------------------------------------------------------------

typedef struct fd_entry_s fd_entry_t;

struct fd_entry_s {
    fd_entry_t* next;
    fd_entry_t* prev;
    //
    fdd_service_input* input_service;
    fdd_service_output* output_service;
    void* socket;
    int fd;
};

// ------------------------------------------------------------

static void* f_poller = NULL;

static unsigned int f_entries_count = 0;
static fd_entry_t* f_entries = NULL;
static fd_entry_t* f_unused_entries = NULL;
static fd_entry_t* f_removed_entries = NULL;

//

static void list_push(fd_entry_t** headp, fd_entry_t* entry)
{
    if (*headp == NULL) {
        entry->next = NULL;
    }
    else {
        (*headp)->prev = entry;
        entry->next = *headp;
    }

    entry->prev = NULL;
    *headp = entry;
}

static void list_remove(fd_entry_t** headp, fd_entry_t* entry)
{
    if (entry->next) {
        entry->next->prev = entry->prev;
    }

    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    else {
        *headp = entry->next;
    }

    entry->next = NULL;
    entry->prev = NULL;
}

static fd_entry_t* list_pop(fd_entry_t** headp)
{
    fd_entry_t* entry = *headp;

    if (entry != NULL) {
        list_remove(headp, entry);
    }

    return entry;
}

// ------------------------------------------------------------

static bool allocate_new_entries()
{
    const fde_node_t* ectx = fde_push_context(this_error_context);
    if (!ectx)
        return false;
    //

    if (f_unused_entries != NULL) {
        fde_push_consistency_failure("unused entries exist");
        return false;
    }

    //

    const size_t byte_size = sizeof(fd_entry_t) * EntryAllocationBlock;
    fd_entry_t* new_entries = malloc(byte_size);

    if (!new_entries) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return false;
    }

    //

    memset(new_entries, 0, byte_size);

    for (int e = 0;
         e < EntryAllocationBlock;
         ++e)
    {
        list_push(&f_unused_entries,
                  new_entries + e);
    }

    return fde_pop_context(this_error_context, ectx);
}

static fd_entry_t* find_entry_by_fd(int fd)
{
    for (fd_entry_t* entry = f_entries;
         entry != NULL;
         entry = entry->next)
    {
        if (entry->fd == fd)
            return entry;
    }

    return NULL;
}

static fd_entry_t* find_entry_by_socket(void* socket)
{
    if (socket == NULL) {
        return NULL;
    }

    for (fd_entry_t* entry = f_entries;
         entry != NULL;
         entry = entry->next)
    {
        if (entry->socket == socket)
            return entry;
    }

    return NULL;
}

static void remove_entry(fd_entry_t* old_entry)
{
    old_entry->socket = NULL;
    old_entry->fd = -1;

    // move old_entry from f_entries to f_removed_entries

    list_remove(&f_entries,
                old_entry);

    list_push(&f_removed_entries,
              old_entry);
}

static void reuse_removed_entries(void)
{
    if (f_removed_entries == NULL) {
        return;
    }

    // splice all entries from f_removed_entries to f_unused_entries

    if (f_unused_entries != NULL)
    {
        fd_entry_t* last_removed = f_removed_entries;

        while (last_removed->next != NULL) {
            last_removed = last_removed->next;
        }

        last_removed->next = f_unused_entries;
        f_unused_entries->prev = last_removed;
    }

    f_unused_entries = f_removed_entries;
    f_removed_entries = NULL;
}

// ------------------------------------------------------------

static bool ZMQ_init(void)
{
    if (f_poller != NULL) {
        return true;
    }

    //
    const fde_node_t* ectx = fde_push_context(this_error_context);
    if (!ectx)
        return false;
    //

    f_poller = zmq_poller_new();

    if (f_poller == NULL) {
        fde_push_resource_failure("ZeroMQ poller");
        return false;
    }

    return fde_pop_context(this_error_context, ectx);
}

static bool ZMQ_poll(fdd_msec_t msec)
{
    if (!f_poller) {
        fde_push_consistency_failure("ZMQ_init() not called");
        return false;
    }
    //

    if (f_removed_entries != NULL) {
        reuse_removed_entries();
    }

    //

    const long timeout = (msec == FDD_INFINITE           ? -1L
                          : msec >= (fdd_msec_t)LONG_MAX ? LONG_MAX
                          : (long)msec);

    //

    zmq_poller_event_t events[f_entries_count];

    const int event_count = zmq_poller_wait_all(f_poller, events, f_entries_count, timeout);

    if (event_count < 0)
    {
        const int poll_errno = zmq_errno();

        switch (poll_errno) {
        case EINTR:
        case ETIMEDOUT:
            return true;
        default:
            fde_push_stdlib_error("zmq_poller_wait_all", poll_errno);
            return false;
        }
    }

    //

    for (int e = 0;
         e < event_count;
         ++e)
    {
        const zmq_poller_event_t* event = &events[e];
        const fd_entry_t* entry = event->user_data;

        if (!entry) {
            fde_push_consistency_failure("fd_entry missing user data");
            return false;
        }

        if (event->events & ZMQ_POLLOUT
            && entry->output_service != NULL)
        {
            const bool result = entry->output_service->serv.notify(entry->output_service->serv.context,
                                                                   entry->fd);

            if (!resolve_notify_return(result))
                return false;
        }

        if (event->events & ZMQ_POLLIN
            && entry->input_service != NULL)
        {
            const bool result = entry->input_service->serv.notify(entry->input_service->serv.context,
                                                                  entry->fd);

            if (!resolve_notify_return(result))
                return false;
        }
    }

    //

    if (f_removed_entries != NULL) {
        reuse_removed_entries();
    }

    return true;
}

static bool ZMQ_empty(void)
{
    return f_entries_count == 0;
}

static bool ZMQ_add_input(int fd,
                          fdd_service_input* service)
{
    const fde_node_t* ectx = fde_push_context(this_error_context);
    if (!ectx)
        return false;
    //
    if (f_poller == NULL
        && !ZMQ_init())
    {
        return false;
    }
    if (fd < 0
        || service == NULL)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    fd_entry_t* entry = find_entry_by_fd(fd);
    const bool exists = (entry != NULL);

    if (!exists) {
        if (f_unused_entries == NULL) {
            allocate_new_entries();
        }

        // get entry from f_unused_entries

        entry = list_pop(&f_unused_entries);
        list_push(&f_entries, entry);
        ++f_entries_count;

        // initialize entry

        memset(entry, 0, sizeof(fd_entry_t));
        entry->fd = fd;
    }
    else if (entry->input_service != NULL
             || entry->output_service == NULL)
    {
        fde_push_consistency_failure("error in fd service consistency");
        return false;
    }

    //

    entry->input_service = service;

    //

    const short zmq_events = ((entry->input_service    != NULL ? ZMQ_POLLIN  : 0)
                              | (entry->output_service != NULL ? ZMQ_POLLOUT : 0));

    if (exists) {
        zmq_poller_modify_fd(f_poller, fd, zmq_events);
    }
    else {
        zmq_poller_add_fd(f_poller, fd, entry, zmq_events);
    }

    return fde_pop_context(this_error_context, ectx);
}

static bool ZMQ_add_output(int fd,
                           fdd_service_output* service)
{
    const fde_node_t* ectx = fde_push_context(this_error_context);
    if (!ectx)
        return false;
    //
    if (f_poller == NULL
        && !ZMQ_init())
    {
        return false;
    }
    if (fd < 0
        || service == NULL)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    fd_entry_t* entry = find_entry_by_fd(fd);
    const bool exists = (entry != NULL);

    if (!exists) {
        if (f_unused_entries == NULL) {
            allocate_new_entries();
        }

        // get entry from f_unused_entries

        entry = list_pop(&f_unused_entries);
        list_push(&f_entries, entry);
        ++f_entries_count;

        // initialize entry

        memset(entry, 0, sizeof(fd_entry_t));
        entry->fd = fd;
    }
    else if (entry->input_service == NULL
             || entry->output_service != NULL)
    {
        fde_push_consistency_failure("error in fd service consistency");
        return false;
    }

    //

    entry->output_service = service;

    //

    const short zmq_events = ((entry->input_service    != NULL ? ZMQ_POLLIN  : 0)
                              | (entry->output_service != NULL ? ZMQ_POLLOUT : 0));

    if (exists) {
        zmq_poller_modify_fd(f_poller, fd, zmq_events);
    }
    else {
        zmq_poller_add_fd(f_poller, fd, entry, zmq_events);
    }

    return fde_pop_context(this_error_context, ectx);
}

static bool ZMQ_remove_input(int fd)
{
    const fde_node_t* ectx = fde_push_context(this_error_context);
    if (!ectx)
        return false;
    //
    if (f_poller == NULL
        && !ZMQ_init())
    {
        return false;
    }
    if (fd < 0) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    fd_entry_t* entry = find_entry_by_fd(fd);

    if (!entry) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    else if (entry->input_service == NULL)
    {
        fde_push_consistency_failure("error in fd service consistency");
        return false;
    }

    //

    entry->input_service = NULL;

    //

    const short zmq_events = ((entry->input_service    != NULL ? ZMQ_POLLIN  : 0)
                              | (entry->output_service != NULL ? ZMQ_POLLOUT : 0));

    if (zmq_events > 0) {
        zmq_poller_modify_fd(f_poller, fd, zmq_events);
    }
    else {
        zmq_poller_remove_fd(f_poller, fd);
    }

    //

    remove_entry(entry);
    --f_entries_count;

    return fde_pop_context(this_error_context, ectx);
}

static bool ZMQ_remove_output(int fd)
{
    const fde_node_t* ectx = fde_push_context(this_error_context);
    if (!ectx)
        return false;
    //
    if (f_poller == NULL
        && !ZMQ_init())
    {
        return false;
    }
    if (fd < 0) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    fd_entry_t* entry = find_entry_by_fd(fd);

    if (!entry) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    else if (entry->output_service == NULL)
    {
        fde_push_consistency_failure("error in fd service consistency");
        return false;
    }

    //

    entry->output_service = NULL;

    //

    const short zmq_events = ((entry->input_service    != NULL ? ZMQ_POLLIN  : 0)
                              | (entry->output_service != NULL ? ZMQ_POLLOUT : 0));

    if (zmq_events > 0) {
        zmq_poller_modify_fd(f_poller, fd, zmq_events);
    }
    else {
        zmq_poller_remove_fd(f_poller, fd);
    }

    //

    remove_entry(entry);
    --f_entries_count;

    return fde_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

const fdd_impl_api_t fdd_impl_zmq ={
    .init  = ZMQ_init,
    .poll  = ZMQ_poll,
    .empty = ZMQ_empty,
    //
    .add_input     = ZMQ_add_input,
    .add_output    = ZMQ_add_output,
    .remove_input  = ZMQ_remove_input,
    .remove_output = ZMQ_remove_output,
};

// ------------------------------------------------------------

bool fdx_add_input_zmq(void* zmq_socket,
                       fdd_service_input* service)
{
    const fde_node_t* ectx = fde_push_context(this_error_context);
    if (!ectx)
        return false;
    //
    if (f_poller == NULL
        && !ZMQ_init())
    {
        return false;
    }
    if (zmq_socket == NULL
        || service == NULL)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    fd_entry_t* entry = find_entry_by_socket(zmq_socket);
    const bool exists = (entry != NULL);

    if (!exists) {
        if (f_unused_entries == NULL) {
            allocate_new_entries();
        }

        // get entry from f_unused_entries

        entry = list_pop(&f_unused_entries);
        list_push(&f_entries, entry);
        ++f_entries_count;

        // initialize entry

        memset(entry, 0, sizeof(fd_entry_t));
        entry->socket = zmq_socket;
    }
    else if (entry->input_service != NULL
             || entry->output_service == NULL)
    {
        fde_push_consistency_failure("error in fd service consistency");
        return false;
    }

    //

    entry->input_service = service;

    //

    const short zmq_events = ((entry->input_service    != NULL ? ZMQ_POLLIN  : 0)
                              | (entry->output_service != NULL ? ZMQ_POLLOUT : 0));

    if (exists) {
        zmq_poller_modify(f_poller, zmq_socket, zmq_events);
    }
    else {
        zmq_poller_add(f_poller, zmq_socket, entry, zmq_events);
    }

    return fde_pop_context(this_error_context, ectx);
}

bool fdx_add_output_zmq(void* zmq_socket,
                        fdd_service_output* service)
{
    const fde_node_t* ectx = fde_push_context(this_error_context);
    if (!ectx)
        return false;
    //
    if (f_poller == NULL
        && !ZMQ_init())
    {
        return false;
    }
    if (zmq_socket == NULL
        || service == NULL)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    fd_entry_t* entry = find_entry_by_socket(zmq_socket);
    const bool exists = (entry != NULL);

    if (!exists) {
        if (f_unused_entries == NULL) {
            allocate_new_entries();
        }

        // get entry from f_unused_entries

        entry = list_pop(&f_unused_entries);
        list_push(&f_entries, entry);
        ++f_entries_count;

        // initialize entry

        memset(entry, 0, sizeof(fd_entry_t));
        entry->socket = zmq_socket;
    }
    else if (entry->input_service == NULL
             || entry->output_service != NULL)
    {
        fde_push_consistency_failure("error in fd service consistency");
        return false;
    }

    //

    entry->output_service = service;

    //

    const short zmq_events = ((entry->input_service    != NULL ? ZMQ_POLLIN  : 0)
                              | (entry->output_service != NULL ? ZMQ_POLLOUT : 0));

    if (exists) {
        zmq_poller_modify(f_poller, zmq_socket, zmq_events);
    }
    else {
        zmq_poller_add(f_poller, zmq_socket, entry, zmq_events);
    }

    return fde_pop_context(this_error_context, ectx);
}

bool fdx_remove_input_zmq(void* zmq_socket)
{
    const fde_node_t* ectx = fde_push_context(this_error_context);
    if (!ectx)
        return false;
    //
    if (f_poller == NULL
        && !ZMQ_init())
    {
        return false;
    }
    if (zmq_socket == NULL) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    fd_entry_t* entry = find_entry_by_socket(zmq_socket);

    if (!entry) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    else if (entry->input_service == NULL)
    {
        fde_push_consistency_failure("error in fd service consistency");
        return false;
    }

    //

    entry->input_service = NULL;

    //

    const short zmq_events = ((entry->input_service    != NULL ? ZMQ_POLLIN  : 0)
                              | (entry->output_service != NULL ? ZMQ_POLLOUT : 0));

    if (zmq_events > 0) {
        zmq_poller_modify(f_poller, zmq_socket, zmq_events);
    }
    else {
        zmq_poller_remove(f_poller, zmq_socket);
    }

    //

    remove_entry(entry);
    --f_entries_count;

    return fde_pop_context(this_error_context, ectx);
}

bool fdx_remove_output_zmq(void* zmq_socket)
{
    const fde_node_t* ectx = fde_push_context(this_error_context);
    if (!ectx)
        return false;
    //
    if (f_poller == NULL
        && !ZMQ_init())
    {
        return false;
    }
    if (zmq_socket == NULL) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    fd_entry_t* entry = find_entry_by_socket(zmq_socket);

    if (!entry) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    else if (entry->output_service == NULL)
    {
        fde_push_consistency_failure("error in fd service consistency");
        return false;
    }

    //

    entry->output_service = NULL;

    //

    const short zmq_events = ((entry->input_service    != NULL ? ZMQ_POLLIN  : 0)
                              | (entry->output_service != NULL ? ZMQ_POLLOUT : 0));

    if (zmq_events > 0) {
        zmq_poller_modify(f_poller, zmq_socket, zmq_events);
    }
    else {
        zmq_poller_remove(f_poller, zmq_socket);
    }

    //

    remove_entry(entry);
    --f_entries_count;

    return fde_pop_context(this_error_context, ectx);
}
