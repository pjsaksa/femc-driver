/* Femc Driver
 * Copyright (C) 2015-2020 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "echo3.h"

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

enum { this_error_context = fd_demo_context_echo3 };

//

enum { buffer_size = 4000 };

typedef struct {
    fdu_bufio_buffer* input_buffer;
    fdu_bufio_buffer* output_buffer;
} echo3_service_t;

// ------------------------------------------------------------

static bool input_data_moved(fdu_bufio_buffer* input, echo3_service_t* service)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;
    //
    FDE_ASSERT( input , "!input", false );
    FDE_ASSERT( service , "!service", false );
    FDE_ASSERT( service->input_buffer , "!service->input_buffer", false );
    FDE_ASSERT( service->output_buffer , "!service->output_buffer", false );
    FDE_ASSERT( !fdu_bufio_is_closed(input) , "fdu_bufio_is_closed(input)", false );
    //

    fdu_bufio_buffer* output = service->output_buffer;

    if (fdu_bufio_is_closed(output)) {
        fdu_bufio_close(input);
        return fde_safe_pop_context(this_error_context, ectx);
    }

    output->filled = input->filled;

    const bool stay_open = fdu_bufio_touch(output);

    return fde_safe_pop_context(this_error_context, ectx)
        && stay_open;
}

// ------------------------------------------------------------

static bool output_data_moved(fdu_bufio_buffer* output, echo3_service_t* service)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;
    //
    FDE_ASSERT( output , "!output", false );
    FDE_ASSERT( service , "!service", false );
    FDE_ASSERT( service->output_buffer , "!service->output_buffer", false );
    FDE_ASSERT( service->input_buffer , "!service->input_buffer", false );
    FDE_ASSERT( !fdu_bufio_is_closed(output) , "fdu_bufio_is_closed(output)", false );
    //

    fdu_bufio_buffer* input = service->input_buffer;

    if (output->filled != input->filled) {
        input->filled = output->filled;

        if (!fdu_bufio_touch(input))
            return false;
    }

    if (fdu_bufio_is_closed(input)
        && fdu_bufio_is_empty(input))
    {
        return fde_safe_pop_context(this_error_context, ectx)
            && !fdu_bufio_is_empty(output);
    }

    return fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static void input_closed(fdu_bufio_buffer* input, echo3_service_t* service, int fd, int read_error)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return;
    //
    FDE_ASSERT( input , "!input", );
    FDE_ASSERT( service , "!service", );
    FDE_ASSERT( fd >= 0 , "fd < 0", );
    FDE_ASSERT( service->input_buffer , "!service->input_buffer", );
    FDE_ASSERT( service->output_buffer , "!service->output_buffer", );
    FDE_ASSERT( fdu_bufio_is_closed(input) , "!fdu_bufio_is_closed(input)", );
    //

    if (read_error)
        fprintf(FDD_ACTIVE_LOGFILE, "echo3:read: %s\n", strerror(read_error));

    fdu_bufio_buffer* output = service->output_buffer;

    if (fdu_bufio_is_closed(output)) {
        free(service);
        return;
    }

    if (read_error
        || (fdu_bufio_is_empty(input)
            && fdu_bufio_is_empty(output)))
    {
        fdu_bufio_close(output);
        return;
    }

    shutdown(fd, SHUT_RD);
    fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static void output_closed(fdu_bufio_buffer* output, echo3_service_t* service, int fd, int write_error)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return;
    //
    FDE_ASSERT( output , "!output", );
    FDE_ASSERT( service , "!service", );
    FDE_ASSERT( fd >= 0 , "fd < 0", );
    FDE_ASSERT( service->input_buffer , "!service->input_buffer", );
    FDE_ASSERT( service->output_buffer , "!service->output_buffer", );
    FDE_ASSERT( fdu_bufio_is_closed(output) , "!fdu_bufio_is_closed(output)", );
    //

    if (write_error)
        fprintf(FDD_ACTIVE_LOGFILE, "echo3:write: %s\n", strerror(write_error));

    fdu_safe_close(fd);

    fdu_bufio_buffer* input = service->input_buffer;

    if (fdu_bufio_is_closed(input)) {
        free(service);
        return;
    }

    fdu_bufio_close(input);
    fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static bool new_echo3_service(void* UNUSED(context), int fd)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    //

    enum { sizeof_echo3_service = sizeof(echo3_service_t) };

    const size_t total_size = (sizeof_echo3_service
                               + 2 * sizeof_fdu_bufio_service
                               + buffer_size);

    //

    unsigned char* alloc = malloc(total_size);

    if (!alloc) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        fdu_safe_close(fd);
        return false;
    }

    echo3_service_t* service = (echo3_service_t*) alloc;
    unsigned char* counter = alloc + sizeof_echo3_service;

    const fdu_memory_area
        input_service_memory    = init_memory_area_cont(&counter, sizeof_fdu_bufio_service),
        output_service_memory   = init_memory_area_cont(&counter, sizeof_fdu_bufio_service),
        buffer_memory           = init_memory_area_cont(&counter, buffer_size);

    fdu_bufio_buffer* is = 0;
    fdu_bufio_buffer* os = 0;

    if ((is =fdu_new_input_bufio_inplace(fd,
                                         input_service_memory,
                                         buffer_memory,
                                         service,
                                         (fdu_bufio_notify_func)input_data_moved,
                                         (fdu_bufio_close_func)input_closed))
        && (os =fdu_new_output_bufio_inplace(fd,
                                             output_service_memory,
                                             buffer_memory,
                                             service,
                                             (fdu_bufio_notify_func)output_data_moved,
                                             (fdu_bufio_close_func)output_closed)))
    {
        service->input_buffer = is;
        service->output_buffer = os;

        return fde_safe_pop_context(this_error_context, ectx);   // <-- normal exit
    }

    fdu_bufio_close(os);
    fdu_bufio_close(is);
    free(service);

    fdu_safe_close(fd);
    return false;
}

// ------------------------------------------------------------

bool echo3_start(unsigned short requested_port)
{
    const fde_node_t* ectx;
    int server_fd;

    fprintf(FDD_ACTIVE_LOGFILE, "starting echo3 in port %hu\n", requested_port);

    return (ectx =fde_push_context(this_error_context))
        && (server_fd =fdu_listen_inet4(requested_port, FDU_SOCKET_LOCAL)) >= 0
        && fdu_auto_accept_connection(server_fd, new_echo3_service, 0)
        && fde_pop_context(this_error_context, ectx);
}
