// Femc Driver - site app
// Copyright (C) 2018-2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

#include "../../utils.h"

#include <stdbool.h>

//typedef bool (*fdu_bufio_notify_func)(fdu_bufio_buffer*, void* context);
//typedef void (*fdu_bufio_close_func)(fdu_bufio_buffer*, void* context, int fd, int error);


bool fda_site_parser_client_got_input(fdu_bufio_buffer* input,
                                      void* connection);

bool fda_site_parser_client_got_output(fdu_bufio_buffer* output,
                                       void* connection);

void fda_site_parser_client_input_closed(fdu_bufio_buffer* input,
                                         void* connection,
                                         int fd,
                                         int read_error);

void fda_site_parser_client_output_closed(fdu_bufio_buffer* output,
                                          void* connection,
                                          int fd,
                                          int write_error);
