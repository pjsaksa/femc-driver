// Femc Driver - site app
// Copyright (C) 2018-2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

#include "../../http.fwd.h"

#include <stdbool.h>

typedef struct {
    unsigned short port;
    const fdu_http_ops_t *const http_ops;
} fda_site_config;

bool site_start(const fda_site_config *);
