/* Femc Driver
 * Copyright (C) 2015-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "echo1.h"

#include "../generic.h"
#include "../dispatcher.h"
#include "../error_stack.h"
#include "../utils.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

enum { this_error_context = fd_demo_context_echo1 };

//

enum { BufferSize = 4000 };

typedef struct {
    int fd;

    char buffer[BufferSize];
    unsigned int filled;

    bool can_read;
    bool can_write;
    bool input_closed;

    fdd_service_input input_service;
    fdd_service_output output_service;
} echo1_service_t;

//

static bool echo1_input(void*, int);

// ------------------------------------------------------------

static bool free_echo1_service(echo1_service_t* service)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    if (!service
        || service->fd < 0)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    //

    if (!service->input_closed && !service->can_read)
        fdd_remove_input(service->fd);
    if (!service->can_write)
        fdd_remove_output(service->fd);
    fdu_safe_close(service->fd);

    free(service);

    return fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static bool echo1_output(void* service_v, int fd)
{
    echo1_service_t* service = (echo1_service_t*) service_v;

    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    if (!service || fd < 0) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    FDE_ASSERT( fd == service->fd , "fd != service->fd" , false );
    FDE_ASSERT( service->filled <= BufferSize , "service->filled > BufferSize" , false );
    //

    if (!service->filled) {
        service->can_write = true;
        return fdd_remove_output(fd)
            && fde_safe_pop_context(this_error_context, ectx);
    }

    const int i = write(fd, service->buffer, service->filled);

    if (i > 0) {
        FDE_ASSERT_DEBUG( (unsigned int)i <= service->filled , "i > service->filled" , false );
        //

        service->filled -= i;

        if (service->filled)
            memmove(service->buffer, &service->buffer[i], service->filled);

        if (service->can_write) {
            service->can_write = false;
            fdd_add_output(fd, &service->output_service);
        }

        if (service->input_closed) {
            if (!service->filled
                && !free_echo1_service(service))
            {
                return false;
            }
        }
        else if (service->can_read) {
            return echo1_input(service, fd)
                && fde_safe_pop_context(this_error_context, ectx);
        }
    }
    else if (errno != EINTR && errno != EAGAIN) {
        if (errno != EPIPE)
            fde_push_stdlib_error("write", errno);

        free_echo1_service(service);
        return false;
    }

    return fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static bool echo1_input(void* service_v, int fd)
{
    echo1_service_t* service = (echo1_service_t*) service_v;

    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    if (!service || fd < 0) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    FDE_ASSERT( fd == service->fd , "fd != service->fd" , false );
    FDE_ASSERT( service->filled <= BufferSize , "service->filled > BufferSize" , false );
    FDE_ASSERT( !service->input_closed , "service->input_closed" , false );
    //

    if (service->filled == BufferSize) {
        service->can_read = true;
        return fdd_remove_input(fd)
            && fde_safe_pop_context(this_error_context, ectx);
    }

    const int i = read(fd, &service->buffer[service->filled], BufferSize - service->filled);

    if (service->can_read) {
        service->can_read = false;
        fdd_add_input(fd, &service->input_service);
    }

    if (i > 0) {
        FDE_ASSERT_DEBUG( i <= BufferSize , "i > BufferSize" , false );
        //

        service->filled += i;

        FDE_ASSERT_DEBUG( service->filled <= BufferSize ,
                          "service->filled > BufferSize" ,
                          false );

        if (service->can_write)
            return echo1_output(service, fd)
                && fde_safe_pop_context(this_error_context, ectx);
    }
    else if (i<0) {
        if (errno != EINTR && errno != EAGAIN) {
            fde_push_stdlib_error("read", errno);
            free_echo1_service(service);
            return false;
        }
    }
    else /*if (!i)*/ {
        if (service->filled) {
            service->input_closed = true;

            if (!service->can_read)
                fdd_remove_input(fd);
        }
        else if (!free_echo1_service(service))
            return false;
    }

    return fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static bool new_echo1_service(void* UNUSED(context), int fd)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;
    //

    echo1_service_t* service = malloc(sizeof(echo1_service_t));

    if (!service) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        fdu_safe_close(fd);
        return false;
    }

    service->fd           = fd;
    service->filled       = 0;
    service->can_read     = false;
    service->can_write    = false;
    service->input_closed = false;

    fdd_init_service_input (&service->input_service,  service, &echo1_input);
    fdd_init_service_output(&service->output_service, service, &echo1_output);

    if (!fdd_add_input(fd, &service->input_service)
        || !fdd_add_output(fd, &service->output_service))
    {
        free(service);
        fdu_safe_close(fd);
        return false;
    }

    return fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

bool echo1_start(unsigned short requested_port)
{
    const fde_node_t* ectx;
    int server_fd;

    fprintf(FDD_ACTIVE_LOGFILE, "starting echo1 in port %hu\n", requested_port);

    return (ectx =fde_push_context(this_error_context))
        && (server_fd =fdu_listen_inet4(requested_port, FDU_SOCKET_LOCAL)) >= 0
        && fdu_auto_accept_connection(server_fd, new_echo1_service, 0)
        && fde_safe_pop_context(this_error_context, ectx);
}
