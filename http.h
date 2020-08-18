// Femc Driver
// Copyright (C) 2015-2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

#include "http.fwd.h"
#include "error_stack.fwd.h"

#include <stdbool.h>
#include <stdint.h>

//

typedef struct {
    fdu_http_parser_progress_t  progress;
    uint32_t                    content_loaded;
} fdu_http_parser_state_t;

typedef struct {
    fdu_http_method_t  method;
    fdu_http_version_t version;
    bool               closing;
    uint32_t           content_length;
    unsigned char      content_type[64];
} fdu_http_message_state_t;

//

typedef bool (*fdu_http_parse_request_url_func)(void* context,
                                                fdu_http_method_t method,
                                                const unsigned char* start,
                                                const unsigned char* end);
typedef bool (*fdu_http_parse_request_version_func)(void* context,
                                                    fdu_http_version_t version);
typedef bool (*fdu_http_parse_request_header_func)(void* context,
                                                   unsigned char* start,
                                                   unsigned char* end);
typedef bool (*fdu_http_parse_request_content_func)(void* context,
                                                    unsigned char* start,
                                                    unsigned char* end);

struct fdu_http_ops_t_ {
    fdu_http_parse_request_url_func     parse_url;
    fdu_http_parse_request_version_func parse_version;
    fdu_http_parse_request_header_func  parse_header;
    fdu_http_parse_request_content_func parse_content;
};

typedef struct {
    void* context;
    const fdu_http_ops_t* http_ops;
    //
    fdu_http_parser_state_t  parser_state;
    fdu_http_message_state_t message_state;
} fdu_http_request_parser_t;

//

void fdu_clear_http_request_parser(fdu_http_request_parser_t*);
void fdu_init_http_request_parser(fdu_http_request_parser_t*,
                                  void* context,
                                  const fdu_http_ops_t* http_ops);

// request

bool fdu_http_parse_request(fdu_http_request_parser_t* parser,
                            unsigned char**,
                            unsigned char**);  // start, end

// response

extern const char* fdu_http_default_error_message;

bool fdu_http_conjure_error_response(fdu_http_request_parser_t* parser,
                                     unsigned int error_code,
                                     const char* error_message,
                                     unsigned char**,
                                     const unsigned char*);  // start, end
