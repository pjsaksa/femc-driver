/* Femc Driver
 * Copyright (C) 2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "dispatcher.impl.h"
#include "error_stack.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum { this_error_context = fdd_context_select };

// ------------------------------------------------------------

static fd_block_node_t* fd_block = 0;
static unsigned int fd_block_size = 0;

static fd_set cached_fd_r;
static fd_set cached_fd_w;
static fd_set current_fd_r;
static fd_set current_fd_w;

static int nfds_r = 0;
static int nfds_w = 0;

static bool resize_fd_block(unsigned int fd)
{
    unsigned int new_size = fd_block_size;

    if (!new_size) {
        FD_ZERO(&cached_fd_r);
        FD_ZERO(&cached_fd_w);
        FD_ZERO(&current_fd_r);
        FD_ZERO(&current_fd_w);

        new_size = 64;
    }

    while (new_size <= fd)
        new_size *= 2;
    if (new_size <= fd_block_size)
        return true;

    fd_block_node_t* new_block = malloc(new_size * sizeof(fd_block_node_t));

    if (!new_block) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return false;
    }

    memcpy(new_block, fd_block, fd_block_size * sizeof(fd_block_node_t));
    memset(new_block + fd_block_size, 0, (new_size - fd_block_size) * sizeof(fd_block_node_t));

    free(fd_block);
    fd_block = new_block;
    fd_block_size = new_size;
    return true;
}

bool dispatcher_poll(fdd_msec_t msec)
{
    const int       nfds = (nfds_r > nfds_w) ? nfds_r : nfds_w;
    struct timeval  tv;
    struct timeval* tv_ptr = 0;

    current_fd_r = cached_fd_r;
    current_fd_w = cached_fd_w;

    if (msec < FDD_INFINITE) {
        tv_ptr = &tv;
        tv.tv_sec = msec/1000;
        tv.tv_usec = (msec%1000)*1000;
    }

    int fd_count = select(nfds, &current_fd_r, &current_fd_w, 0, tv_ptr);

    if (!fd_count) return true;
    else if (fd_count < 0) {
        if (errno == EINTR || errno == EAGAIN)
            return true;

        fde_push_stdlib_error("select", errno);
        return false;
    }

    //

    for (int fd = 0; fd < nfds_r; ++fd)
    {
        if (!FD_ISSET(fd, &current_fd_r)) continue;
        FD_CLR(fd, &current_fd_r);

        fdd_service_input* handler = fd_block[fd].input_handler;

        if (!resolve_notify_return(handler->serv.notify(handler->serv.context, fd)))
            return false;

        if (!--fd_count) return true;
    }

    for (int fd = 0; fd < nfds_w; ++fd)
    {
        if (!FD_ISSET(fd, &current_fd_w)) continue;
        FD_CLR(fd, &current_fd_w);

        fdd_service_output* handler = fd_block[fd].output_handler;

        if (!resolve_notify_return(handler->serv.notify(handler->serv.context, fd)))
            return false;

        if (!--fd_count) return true;
    }

    return true;
}

bool dispatcher_empty(void)
{
    return nfds_r == 0
        && nfds_w == 0;
}

bool fdd_add_input(int fd, fdd_service_input* service)
{
#ifdef FD_DEBUG
    if (!service
        || fd < 0)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
#endif

    if ((unsigned int)fd >= fd_block_size
        && !resize_fd_block(fd))
    {
        return false;
    }

    if (fd_block[fd].input_handler) {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_io_handler_corrupted);
        return false;
    }

    fd_block[fd].input_handler = service;

    FD_SET(fd, &cached_fd_r);
    FD_CLR(fd, &current_fd_r);

    if (!nfds_r || nfds_r < fd + 1)
        nfds_r = fd + 1;

    return true;
}

bool fdd_add_output(int fd, fdd_service_output* service)
{
#ifdef FD_DEBUG
    if (!service
        || fd < 0)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
#endif

    if ((unsigned int)fd >= fd_block_size
        && !resize_fd_block(fd))
    {
        return false;
    }

    if (fd_block[fd].output_handler) {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_io_handler_corrupted);
        return false;
    }

    fd_block[fd].output_handler = service;

    FD_SET(fd, &cached_fd_w);
    FD_CLR(fd, &current_fd_w);

    if (!nfds_w || nfds_w < fd + 1)
        nfds_w = fd + 1;

    return true;
}

bool fdd_remove_input(int fd)
{
#ifdef FD_DEBUG
    if (fd < 0) {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
#endif

    if ((unsigned int)fd >= fd_block_size)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_io_handler_corrupted);
        return false;
    }

    //

    fd_block[fd].input_handler = 0;

    FD_CLR(fd, &cached_fd_r);
    FD_CLR(fd, &current_fd_r);

    while (nfds_r && !fd_block[nfds_r-1].input_handler)
        --nfds_r;

    return true;
}

bool fdd_remove_output(int fd)
{
#ifdef FD_DEBUG
    if (fd < 0) {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
#endif

    if ((unsigned int)fd >= fd_block_size)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_io_handler_corrupted);
        return false;
    }

    //

    fd_block[fd].output_handler = 0;

    FD_CLR(fd, &cached_fd_w);
    FD_CLR(fd, &current_fd_w);

    while (nfds_w && !fd_block[nfds_w-1].output_handler)
        --nfds_w;

    return true;
}
