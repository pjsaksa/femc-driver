/* Femc Driver - site app
 * Copyright (C) 2018-2020 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "parser.h"
#include "connection.h"

#include "../../dispatcher.h"
#include "../../error_stack.h"
#include "../../generic.h"
#include "../../http.h"
#include "../../utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    this_error_context = fda_site_parser,
};

//

static void free_connection(fda_site_connection* connection)
{
    const int fd = connection->client_output->fd;

    if (fd >= 0)
        fdu_safe_close(fd);

    free(connection);
}

// ------------------------------------------------------------

bool fda_site_parser_client_got_input(fdu_bufio_buffer* input,
                                      void* connection_void)
{
    printf("client_got_input()\n");
    //


    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    //

    if (!input
        || !connection_void)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    // -----

    fda_site_connection* const connection = (fda_site_connection*) connection_void;
    fdu_bufio_buffer* const output = connection->client_output;

    if (fdu_bufio_is_closed(output)) {
        fdu_bufio_close(input);
        return fde_safe_pop_context(this_error_context, ectx);
    }

    // -----

    unsigned char* begin = input->data;
    unsigned char* end   = input->data + input->filled;

    const bool parsing_status = fdu_http_parse_request(&connection->parser, &begin, &end);

    const bool was_changed = (input->filled != (unsigned long)(end - begin));

    if (was_changed) {
        input->filled = end - begin;

        if (input->filled && begin > input->data)
            memmove(input->data, begin, input->filled);

        if (!fdu_bufio_touch(input))
            return false;
    }

    fde_node_t* err = 0;

    if (parsing_status)
    {
        connection->state = s_working;
        fdu_clear_http_request_parser(&connection->parser);

        // start worker
        // send request to worker
        // clear http parser
    }
    else if ((err =fde_get_last_error(fde_node_resource_failure_b))
             && err->id == fde_resource_buffer_underflow)
    {
        // wait for more data

        fde_reset_context(this_error_context, ectx);
    }
    else if ((err =fde_get_last_error(fde_node_http_error_b)))
    {
        fde_reset_context(this_error_context, ectx);
        //

        connection->parser.message_state.closing = true;

        fdu_bufio_buffer* output = connection->client_output;
        unsigned char* start     = output->data + output->filled;

        if (!fdu_http_conjure_error_response(&connection->parser,
                                             err->id,
                                             err->message,
                                             &start,
                                             output->data + output->size))
        {
            printf("fdu_http_conjure_error_response() failed\n");
            return false;
        }

        output->filled = output->data - start;

        fdu_clear_http_request_parser(&connection->parser);

        const bool stay_open = fdu_bufio_touch(output);
        return fde_safe_pop_context(this_error_context, ectx)
            && stay_open;
    }
    else {
        return false;
    }

    return fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

bool fda_site_parser_client_got_output(fdu_bufio_buffer* output,
                                       void* connection_void)
{
    printf("client_got_output()\n");
    //

    fda_site_connection* const connection = (fda_site_connection*) connection_void;

    return connection->state != s_closing
        || output->filled;
}

// ------------------------------------------------------------

void fda_site_parser_client_input_closed(fdu_bufio_buffer* input,
                                         void* connection_void,
                                         int fd,
                                         int read_error)
{
    printf("client_input_closed()\n");
    //


    const fde_node_t* ectx =0;

    if (!(ectx =fde_push_context(this_error_context)))
        return;

    if (!input
        || !connection_void
        || fd < 0)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return;
    }

    // -----

    if (read_error)
        fprintf(stderr, "read: %s\n", strerror(read_error));

    fda_site_connection* const connection = (fda_site_connection*) connection_void;
    fdu_bufio_buffer* const output = connection->client_output;

    FDE_ASSERT( input == connection->client_input , "input != connection->client_input" , );

    if (fdu_bufio_is_closed(output)) {
        free_connection(connection);
        return;
    }

/* --- need "processing" flag in here

    if (read_error
        || (fdu_bufio_is_empty(input)
            && fdu_bufio_is_empty(output)))
    {
        fdu_bufio_close(output);
        return;
    }
*/

    free_connection(connection);
    fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

void fda_site_parser_client_output_closed(fdu_bufio_buffer* output,
                                          void* connection_void,
                                          int fd,
                                          int write_error)
{
    printf("client_output_closed()\n");
    //


    const fde_node_t* ectx = 0;

    if (!(ectx =fde_push_context(this_error_context)))
        return;

    if (!output
        || !connection_void
        || fd < 0)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return;
    }

    //

    if (write_error)
        fprintf(stderr, "site/parser:write: %s\n", strerror(write_error));

    fda_site_connection* connection = (fda_site_connection*) connection_void;

    FDE_ASSERT( output == connection->client_output , "output != connection->client_output" , );

/*
    connection->client_output = 0;

    if (!connection->client_input) {
        free_connection(connection);
    }
*/

    free_connection(connection);
    fde_safe_pop_context(this_error_context, ectx);
}
