/* Femc Driver
 * Copyright (C) 2015-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "date.h"

#include "generic.h"
#include "error_stack.h"
#include "utils.h"

#include <time.h>
#include <errno.h>
#include <stdio.h>

enum { this_error_context =fd_demo_context_date };

//

static bool date_new_connection(void* UNUSED(context), int fd)
{
    const fde_node_t *ectx;
    if (!(ectx =fde_push_context(this_error_context)))
        return false;
    //

    enum { buffer_size = 20 };

    unsigned char date_buf[buffer_size];
    size_t bytes =0;

    {
        struct tm tm;
        time_t now =time(0);

        if (now < 0) {
            fde_push_stdlib_error("time", errno);
            fdu_safe_close(fd);
            return false;
        }

        bytes =strftime((char *)date_buf, buffer_size,
                        "%m%d%H%M%Y.%S\n",
                        localtime_r(&now, &tm));

        if (!bytes) {
            fde_push_stdlib_error("strftime", errno);
            fdu_safe_close(fd);
            return false;
        }

        if (bytes >= buffer_size)
            bytes =buffer_size-1;

        date_buf[bytes] =0;
    }

    //

    const bool write_ok =fdu_safe_write(fd, date_buf, date_buf+bytes);

    return fdu_safe_close(fd)
        && write_ok
        && fde_pop_context(this_error_context, ectx);
}

// *********************************************************

bool date_start(unsigned short requested_port)
{
    const fde_node_t *ectx;
    int server_fd;

    fprintf(FDD_ACTIVE_LOGFILE, "starting date in port %hu\n", requested_port);

    return (ectx =fde_push_context(this_error_context))
        && (server_fd =fdu_listen_inet4(requested_port, FDU_SOCKET_LOCAL)) >= 0
        && fdu_auto_accept_connection(server_fd, date_new_connection, 0)
        && fde_safe_pop_context(this_error_context, ectx);
}
