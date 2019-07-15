/* Femc Driver - site app
 * Copyright (C) 2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_APP_SITE_CONFIG_HEADER
#define FEMC_DRIVER_APP_SITE_CONFIG_HEADER

#include "http.fwd.h"

typedef struct {
    unsigned short port;
    const fdu_http_spec_t* http_spec;
    unsigned int sizeof_request;
} fda_site_config;

#endif
