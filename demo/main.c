/* Femc Driver
 * Copyright (C) 2015-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "dispatcher.h"
#include "error_stack.h"

#include "timer1.h"
#include "date.h"
#include "echo1.h"
#include "echo2.h"
#include "echo3.h"
#include "route.h"

#include <stdlib.h>

int main(void)
{
    return (true//timer1_start()
            && date_start(10000)
            && echo1_start(10001)
            && echo2_start(10002)
            && echo3_start(10003)
            //
            && route_start(10010, "127.0.0.1", 10000)
            && route_start(10011, "127.0.0.1", 10001)
            && route_start(10012, "127.0.0.1", 10002)
            && route_start(10013, "127.0.0.1", 10003)
            //
            && route_start(10020, "127.0.0.1", 10010)
            && route_start(10021, "127.0.0.1", 10011)
            && route_start(10022, "127.0.0.1", 10012)
            && route_start(10023, "127.0.0.1", 10013)
            //
            && fdd_main(FDD_INFINITE))
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
