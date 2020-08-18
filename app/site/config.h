// Femc Driver - site app
// Copyright (C) 2018-2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

#include "../../http.fwd.h"

typedef struct {
    unsigned short port;
    const fdu_http_spec_t* http_spec;
    unsigned int sizeof_request;
} fda_site_config;
