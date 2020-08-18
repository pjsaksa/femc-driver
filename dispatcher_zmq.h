// Femc Driver
// Copyright (C) 2015-2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

#include "dispatcher.h"

bool fdx_add_input_zmq(void* socket, fdd_service_input* service);
bool fdx_add_output_zmq(void* socket, fdd_service_output* service);
bool fdx_remove_input_zmq(void* socket);
bool fdx_remove_output_zmq(void* socket);
