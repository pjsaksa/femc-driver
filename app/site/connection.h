/* Femc Driver - site app
 * Copyright (C) 2018-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_APP_SITE_CONNECTION_HEADER
#define FEMC_DRIVER_APP_SITE_CONNECTION_HEADER

#include "../../http.h"
#include "../../utils.h"

typedef enum {
    s_working,
    s_parsing_request,
    s_sending_request,
    s_parsing_response,
    s_sending_response,
    //
    s_closing,
} fda_site_connection_state;

typedef struct {
    fda_site_connection_state state;

    fdu_bufio_buffer* client_input;
    fdu_bufio_buffer* client_output;

    fdu_http_request_parser_t parser;
    fdu_memory_area request_memory;

    fdu_bufio_buffer* worker_input;
    fdu_bufio_buffer* worker_output;
} fda_site_connection;


#endif
