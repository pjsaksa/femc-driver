/* Femc Driver
 * Copyright (C) 2015-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "route.h"

#include "../dispatcher.h"
#include "../error_stack.h"
#include "../utils.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>

enum { this_error_context = fd_demo_context_route };

//

enum { buffer_size = 4000 };

typedef struct {
    fdd_service_input input_service;
    const char* target_address;
    unsigned short target_port;
} route_listening_service_t;

// ------------------------------------------------------------

typedef struct two_way_route_t_ two_way_route_t;
typedef struct one_way_route_t_ one_way_route_t;

struct one_way_route_t_ {
    two_way_route_t* context;

    int input_fd;
    int output_fd;

    bool can_read;
    bool can_write;

    fdd_service_input input_service;
    fdd_service_output output_service;

    char buffer[buffer_size];
    unsigned int filled;
};

struct two_way_route_t_ {
    route_listening_service_t* listening_service;

    one_way_route_t there;
    one_way_route_t back;
};

static bool route_output(void*, int fd);
static bool route_close_output(one_way_route_t*, int fd, int error);

// ------------------------------------------------------------

static void free_two_way_route(two_way_route_t* service)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return;
    //

    one_way_route_t* r1 = &service->there;
    one_way_route_t* r2 = &service->back;

    // close fds

    if (r1->input_fd >= 0)       fdu_safe_close(r1->input_fd);
    else if (r2->output_fd >= 0) fdu_safe_close(r2->output_fd);

    if (r1->output_fd >= 0)     fdu_safe_close(r1->output_fd);
    else if (r2->input_fd >= 0) fdu_safe_close(r2->input_fd);

    // remove from dispatcher

    if (r1->input_fd >= 0 && !r1->can_read) fdd_remove_input(r1->input_fd);
    if (r1->output_fd >= 0 && !r1->can_write) fdd_remove_output(r1->output_fd);
    if (r2->input_fd >= 0 && !r2->can_read) fdd_remove_input(r2->input_fd);
    if (r2->output_fd >= 0 && !r2->can_write) fdd_remove_output(r2->output_fd);

    free(service);
    fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static bool route_close_input(one_way_route_t* this, int fd, int error)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;
    if (!this || fd < 0) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //
    two_way_route_t* rr = this->context;
    //
    FDE_ASSERT_DEBUG( rr , "!this->context" , false );
    FDE_ASSERT_DEBUG( this == &rr->there || this == &rr->back , "context corrupted" , false );
    FDE_ASSERT_DEBUG( fd == this->input_fd , "fd != this->input_fd" , false );
    //

    if (error)
        perror("route:read");

    //

    one_way_route_t* other = (this == &rr->there) ? &rr->back : &rr->there;

    const bool other_output_open = (other->output_fd == fd);

    if (other_output_open)
        shutdown(fd, SHUT_RD);
    else
        fdu_safe_close(fd);

    if (this->can_read)
        this->can_read = false;
    else
        fdd_remove_input(fd);

    this->input_fd = -1;

    //

    if (error && other_output_open)
        if (route_close_output(other, fd, 0))
            return true;

    if (this->input_fd < 0
        && this->output_fd < 0
        && other->input_fd < 0
        && other->output_fd < 0)
    {
        free_two_way_route(rr);
        fde_safe_pop_context(this_error_context, ectx);
        return true;
    }

    //

    const bool return_true = (!this->filled
                              && this->output_fd >= 0
                              && route_close_output(this, this->output_fd, 0));

    fde_safe_pop_context(this_error_context, ectx);

    return return_true;
}

static bool route_close_output(one_way_route_t* this, int fd, int error)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;
    if (!this || fd < 0) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //
    two_way_route_t* rr = this->context;
    //
    FDE_ASSERT_DEBUG( rr , "!this->context" , false );
    FDE_ASSERT_DEBUG( this == &rr->there || this == &rr->back , "context corrupted" , false );
    FDE_ASSERT_DEBUG( fd == this->output_fd , "fd != this->output_fd" , false );
    //

    if (error) {
        if (error == EPIPE)
            error = 0;
        else
            perror("route:write");
    }

    //

    one_way_route_t* other = (this == &rr->there) ? &rr->back : &rr->there;

    const bool other_input_open = (other->input_fd == fd);

    if (other_input_open)
        shutdown(fd, SHUT_WR);
    else
        fdu_safe_close(fd);

    if (this->can_write)
        this->can_write = false;
    else
        fdd_remove_output(fd);

    this->output_fd = -1;

    //

    if (error
        && other_input_open
        && route_close_input(other, fd, 0))
    {
        fde_safe_pop_context(this_error_context, ectx);
        return true;
    }

    if (this->input_fd < 0
        && this->output_fd < 0
        && other->input_fd < 0
        && other->output_fd < 0)
    {
        free_two_way_route(rr);
        fde_safe_pop_context(this_error_context, ectx);
        return true;
    }

    //

    const bool return_true = (this->input_fd >= 0
                              && route_close_input(this, this->input_fd, 0));

    fde_safe_pop_context(this_error_context, ectx);

    return return_true;
}

// ------------------------------------------------------------

static bool route_input(void* r1_v, int fd)
{
    one_way_route_t* r1 = (one_way_route_t*) r1_v;

    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;
    if (!r1 || fd < 0) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //
    FDE_ASSERT( fd == r1->input_fd , "fd != r1->input_fd" , false );
    FDE_ASSERT_DEBUG( r1->filled <= buffer_size , "r1->filled > buffer_size (1)" , false );
    //

    if (r1->filled == buffer_size) {
        r1->can_read = true;

        return fde_push_debug_message("in: if (r1->filled == buffer_size)")
            && fdd_remove_input(fd)
            && fde_safe_pop_context(this_error_context, ectx);
    }

    const int i = read(fd, &r1->buffer[r1->filled], buffer_size - r1->filled);

    if (r1->can_read) {
        r1->can_read = false;

        if (!fdd_add_input(fd, &r1->input_service))
            return false;
    }

    if (i > 0) {
        r1->filled += i;

        FDE_ASSERT_DEBUG( r1->filled <= buffer_size , "r1->filled > buffer_size (2)" , false );

        if (r1->can_write) {
            return route_output(r1, r1->output_fd)
                && fde_safe_pop_context(this_error_context, ectx);
        }
    }
    else if (!i) {
        route_close_input(r1, fd, 0);
    }
    else if (errno != EINTR && errno != EAGAIN) {
        route_close_input(r1, fd, errno);
        return false;
    }

    return fde_safe_pop_context(this_error_context, ectx);
}

static bool route_output(void* r1_v, int fd)
{
    one_way_route_t* r1 = (one_way_route_t*) r1_v;

    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;
    if (!r1 || fd < 0) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //
    FDE_ASSERT( fd == r1->output_fd , "fd != r1->output_fd" , false );
    FDE_ASSERT_DEBUG( r1->filled <= buffer_size , "r1->filled corrupted (1)" , false );
    //

    if (!r1->filled) {
        r1->can_write = true;

        return fde_push_debug_message("in: if (!r1->filled)")
            && fdd_remove_output(fd)
            && fde_safe_pop_context(this_error_context, ectx);
    }

    const int i = write(fd, r1->buffer, r1->filled);

    if (i > 0) {
        FDE_ASSERT_DEBUG( (unsigned int)i <= r1->filled , "i > r1->filled" , false );

        r1->filled -= i;

        if (r1->filled)
            memmove(r1->buffer, &r1->buffer[i], r1->filled);

        if (r1->can_read) {
            return route_input(r1, r1->input_fd)
                && fde_safe_pop_context(this_error_context, ectx);
        }
        else if (!r1->filled && r1->input_fd < 0)
            route_close_output(r1, fd, 0);
        else if (r1->can_write) {
            r1->can_write = false;

            if (!fdd_add_output(fd, &r1->output_service))
                return false;
        }
    }
    else if (errno == EPIPE)
    {
        route_close_output(r1, fd, 0);
        return fde_safe_pop_context(this_error_context, ectx);
    }
    else if (errno != EINTR && errno != EAGAIN)
    {
        const int write_errno = errno;

        perror("write");

        route_close_output(r1, fd, write_errno);
        fde_safe_pop_context(this_error_context, ectx);
        return false;
    }

    return fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static bool route_connected(void* rr_v,
                            int fd,
                            int connect_errno)
{
    two_way_route_t* rr = (two_way_route_t*) rr_v;

    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    if (!rr) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    //

    if (fd<0 || connect_errno)
    {
        fde_push_stdlib_error("connect", connect_errno);

        if (fd >= 0) fdu_safe_close(fd);

        fdu_safe_close(rr->there.input_fd);
        free(rr);
        return false;
    }

    rr->there.output_fd = rr->back.input_fd = fd;

    fdd_add_input(rr->there.input_fd, &rr->there.input_service);
    fdd_add_output(rr->there.output_fd, &rr->there.output_service);
    fdd_add_input(rr->back.input_fd, &rr->back.input_service);
    fdd_add_output(rr->back.output_fd, &rr->back.output_service);

    return fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static void route_dns_ready(void* rr_v,
                            const char* address)
{
    two_way_route_t* rr = (two_way_route_t*) rr_v;

    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return;

    //

    if (!rr
        || !rr->listening_service
        || rr->there.input_fd < 0)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return;
    }

    if (!address) {
        fprintf(FDD_ACTIVE_LOGFILE,
                "info: route: DNS query failed: \"%s\"\n",
                rr->listening_service->target_address);

        fdu_safe_close(rr->there.input_fd);
        free(rr);

        fde_safe_pop_context(this_error_context, ectx);
        return; // technically a success
    }

    //

    struct sockaddr_in sa;

    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(rr->listening_service->target_port);
    if (inet_pton(AF_INET, address, &sa.sin_addr) <= 0) {
        fde_push_stdlib_error("inet_pton", errno);
        fdu_safe_close(rr->there.input_fd);
        free(rr);
        return;
    }

    if (!fdu_lazy_connect(&sa,
                          &route_connected,
                          rr,
                          0))
    {
        fdu_safe_close(rr->there.input_fd);
        free(rr);
        return;
    }

    fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static bool new_two_way_route(int fd, route_listening_service_t* listening_service)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;
    //

    two_way_route_t* rr = malloc(sizeof(two_way_route_t));

    if (!rr) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        fdu_safe_close(fd);
        return false;
    }

    rr->listening_service = listening_service;

    rr->there.context = rr->back.context = rr;

    rr->there.input_fd = rr->back.output_fd = fd;
    rr->there.output_fd = rr->back.input_fd = -1;

    rr->there.can_read = rr->there.can_write = false;
    rr->back.can_read = rr->back.can_write = false;

    rr->there.filled = rr->back.filled = 0;

    fdd_init_service_input (&rr->there.input_service,  &rr->there, &route_input);
    fdd_init_service_output(&rr->there.output_service, &rr->there, &route_output);
    fdd_init_service_input (&rr->back.input_service,   &rr->back,  &route_input);
    fdd_init_service_output(&rr->back.output_service,  &rr->back,  &route_output);

    //

    if (fdu_dnsserv_lookup(listening_service->target_address,
                           &route_dns_ready,
                           rr))
    {
        return fde_safe_pop_context(this_error_context, ectx);
    }
    else {
        free(rr);
        fdu_safe_close(fd);
        return false;
    }
}

// ------------------------------------------------------------

static bool route_new_connection(void* listening_service_v,
                                 int fd)
{
    route_listening_service_t* listening_service = (route_listening_service_t*) listening_service_v;

    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return 0;
    //

    int new_socket;

 retry:
    new_socket = accept(fd, 0, 0);

    if (new_socket < 0) {
        if (errno == EINTR)
            goto retry;

        fde_push_stdlib_error("accept", errno);
        return false;
    }

    return new_two_way_route(new_socket, listening_service)
        && fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static bool new_route_listening_service(int fd,
                                        const char* target_address,
                                        unsigned short target_port)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;
    //

    route_listening_service_t* service = malloc(sizeof(route_listening_service_t));

    if (!service) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        fdu_safe_close(fd);
        return false;
    }

    fdd_init_service_input(&service->input_service,
                           service,
                           &route_new_connection);

    service->target_address = target_address;
    service->target_port    = target_port;

    if (!fdd_add_input(fd, &service->input_service)) {
        free(service);
        fdu_safe_close(fd);
        return false;
    }

    return fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

bool route_start(unsigned short requested_port,
                 const char* target_address,
                 unsigned short target_port)
{
    const fde_node_t* ectx;
    int socketfd;

    fprintf(FDD_ACTIVE_LOGFILE, "starting route in port %hu\n", requested_port);

    return (ectx =fde_push_context(this_error_context))
        && (socketfd =fdu_listen_inet4(requested_port, 0)) >= 0
        && new_route_listening_service(socketfd, target_address, target_port)
        && fde_safe_pop_context(this_error_context, ectx);
}
