/* Femc Driver - site app
 * Copyright (C) 2018-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "site.h"

#include "connection.h"
#include "parser.h"

#include "dispatcher.h"
#include "generic.h"
#include "error_stack.h"

#include <stdlib.h>

enum {
    buffer_size        = 4 * 1024,
    this_error_context = fda_site_main,
};

//

static bool new_connection(const fda_site_config* config, int fd)
{
    const fde_node_t* ectx =0;

    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    if (!config
        || fd < 0)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    //

    enum { sizeof_connection = sizeof(fda_site_connection) };

    const size_t total_size = (sizeof_connection
                               + 2 * sizeof_fdu_bufio_service
                               + 2 * buffer_size
                               + config->sizeof_request);

    //

    unsigned char* const alloc = malloc(total_size);

    if (!alloc) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        fdu_safe_close(fd);
        return false;
    }

    fda_site_connection* const connection = (fda_site_connection*) alloc;

    unsigned char* counter = alloc + sizeof_connection;

    const fdu_memory_area
        iserv_mem   = init_memory_area_cont(&counter, sizeof_fdu_bufio_service),
        ibuf_mem    = init_memory_area_cont(&counter, buffer_size),
        oserv_mem   = init_memory_area_cont(&counter, sizeof_fdu_bufio_service),
        obuf_mem    = init_memory_area_cont(&counter, buffer_size);

    fdu_bufio_buffer* is = 0;
    fdu_bufio_buffer* os = 0;

    if ((is =fdu_new_input_bufio_inplace(fd,
                                         iserv_mem,
                                         ibuf_mem,
                                         connection,
                                         fda_site_parser_client_got_input,
                                         fda_site_parser_client_input_closed))
        && (os =fdu_new_output_bufio_inplace(fd,
                                             oserv_mem,
                                             obuf_mem,
                                             connection,
                                             fda_site_parser_client_got_output,
                                             fda_site_parser_client_output_closed)))
    {
        connection->state          = s_parsing_request;
        connection->client_input   = is;
        connection->client_output  = os;
        connection->request_memory = init_memory_area_cont(&counter,
                                                           config->sizeof_request);
        connection->worker_input   = 0;
        connection->worker_output  = 0;

        fdu_init_http_request_parser(&connection->parser,
                                     &connection->request_memory,
                                     config->http_spec);


        return fde_safe_pop_context(this_error_context, ectx);   // <-- normal exit
    }

    fdu_bufio_close(os);
    fdu_bufio_close(is);
    free(connection);

    fdu_safe_close(fd);
    return false;
}

// ------------------------------------------------------------

bool site_start(const fda_site_config* config)
{
    const fde_node_t* ectx = 0;
    int server_fd = -1;

    fprintf(stderr, "starting site in port %hu\n", config->port);

    return (ectx =fde_push_context(this_error_context))
        && (server_fd =fdu_listen_inet4(config->port, 0)) >= 0
        && fdu_auto_accept_connection(server_fd, (fdd_notify_func) new_connection, (void*) config)
        && fde_pop_context(this_error_context, ectx);
}
