// Femc Driver
// Copyright (C) 2016-2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

enum { MaxContentLength = 64 * 1024 };
enum { ContentTypeSize  = 64 };

typedef enum {
    fdu_http_progress_request_line,
    fdu_http_progress_headers,
    fdu_http_progress_content_not_read,
    fdu_http_progress_reading_content,
    fdu_http_progress_done,
} fdu_http_parser_progress_t;

typedef enum {
    fdu_http_method_get = 1,
    fdu_http_method_head,
    fdu_http_method_post,
} fdu_http_method_t;

typedef enum {
    fdu_http_version_1_0 = 1,
    fdu_http_version_1_1,
} fdu_http_version_t;

typedef struct fdu_http_ops_t_s fdu_http_ops_t;
