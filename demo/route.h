/* Femc Driver
 * Copyright (C) 2015-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_ROUTE_HEADER
#define FEMC_DRIVER_ROUTE_HEADER

#include <stdbool.h>

bool route_start(unsigned short,        // local port
                 const char*,           // target address
                 unsigned short);       // target port

#endif
