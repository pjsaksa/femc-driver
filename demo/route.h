// Femc Driver
// Copyright (C) 2015-2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

#include <stdbool.h>

bool route_start(unsigned short,        // local port
                 const char*,           // target address
                 unsigned short);       // target port
