/* Femc Driver
 * Copyright (C) 2015-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#define _GNU_SOURCE

#include "tcplogger.h"

#include "generic.h"
#include "dispatcher.h"
#include "utils.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>

#define TargetPort 6666

#define InputBufferSize 1000

typedef struct
{
    fdd_service_input local_input;
    int fd;

    unsigned char input_buffer[InputBufferSize];
    unsigned int input_length;
}
    tcplogger_service;

static bool tcplogger_read_input(void*, int);

//

static void init_tcplogger_service(tcplogger_service* service, int fd)
{
    service->input_length = 0;
    service->fd = fd;

    fdd_init_service_input(&service->local_input,
                           service,
                           &tcplogger_read_input);
    fdd_add_input(&service->local_input, fd);
}

static void free_tcplogger_service(tcplogger_service* service)
{
    fdd_remove_input(&service->local_input, service->fd);
    close(service->fd);
    free(service);
}

// ------------------------------------------------------------

#if 0
static char* get_time_stamp(void)
{
    static char buffer[30];
    time_t now = time(0);

    strftime(buffer, 30, "%Y-%m-%d %H:%M:%S", localtime(&now));

    return buffer;
}
#endif

// ------------------------------------------------------------

static int server_connect_pending = 0;
static int server_fd = -1;
static fdd_service_input* server_service = 0;

static bool tcplogger_close_server(void* UNUSED(is_error), int UNUSED(dummy2))
{
    fdd_remove_input(server_service, server_fd);

    close(server_fd);
    server_fd = -1;
    free(server_service);
    server_service = 0;

    return true;
}

// ------------------------------------------------------------

static void send_data(tcplogger_service* service)
{
    int rv;
    unsigned char* last_lf = memrchr(service->input_buffer, '\n', service->input_length);

    if (!last_lf) return;
    ++last_lf;

    rv = fdu_safe_write(server_fd, service->input_buffer, last_lf);

    if (rv < 0) {
        fprintf(FDD_ACTIVE_LOGFILE, "TCP-logger: write returned %d (%s), expected %d\n",
                rv, strerror(errno), service->input_length);

        tcplogger_close_server(0, 0);
        free_tcplogger_service(service);
    }
    else if ((unsigned int)rv != service->input_length) {
        fprintf(FDD_ACTIVE_LOGFILE, "TCP-logger: write returned %d, expected %d\n", rv, service->input_length);

        tcplogger_close_server(0, 0);
        free_tcplogger_service(service);
    }
    else /*if (rv == service->input_length)*/ {
        service->input_length -= last_lf - service->input_buffer;
        memmove(service->input_buffer, last_lf, service->input_length);
    }
}

static bool connected(void* service_v,
                      int fd,
                      int connect_errno)
{
    tcplogger_service* service = (tcplogger_service*) service_v;

    server_connect_pending = 0;

    if (fd >= 0 && !connect_errno)
    {
        server_service = malloc(sizeof(fdd_service_input));
        fdd_init_service_input(server_service,
                               server_service,
                               &tcplogger_close_server);
        fdd_add_input(server_service, fd);

        server_fd = fd;

        send_data(service);
    }
    else {
        if (fd >= 0)
            close(fd);

        free_tcplogger_service(service);
    }

    return true;
}

static void process_input(tcplogger_service* service)   // context
{
    if (server_fd >= 0)
        send_data(service);
    else if (!server_connect_pending)
    {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(struct sockaddr_in));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(TargetPort);
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        server_connect_pending = 1;

        if (fdu_lazy_connect(&sa,
                             &connected,
                             service,
                             0) != 0)
        {
            server_connect_pending = 0;
            free_tcplogger_service(service);
        }
    }
}

// ------------------------------------------------------------

static bool tcplogger_read_input(void* service_v, int fd)
{
    tcplogger_service* service = (tcplogger_service*) service_v;

    // read until no more input (read returns 0)

    const int bytes = read(fd,
                           &service->input_buffer[service->input_length],
                           InputBufferSize-service->input_length);
    if (bytes>0) {
        service->input_length += bytes;

        if (memchr(&service->input_buffer[service->input_length-bytes], '\n', bytes))
            process_input(service);
        else if (service->input_length >= InputBufferSize)
            free_tcplogger_service(service);

        return true;
    }
    else if (bytes<0)
    {
        if (errno == EINTR || errno == EAGAIN)
            return true;

        perror("tcplogger: read");

        free_tcplogger_service(service);
        return false;
    }
    else /*if (!bytes)*/ {
        free_tcplogger_service(service);
        return true;
    }
}

static bool tcplogger_new_connection(void* UNUSED(service), int fd)
{
    int new_socket;

    do {
        new_socket = accept(fd, 0, 0);
    } while (new_socket < 0
             && (errno == EINTR
                 || errno == EAGAIN));

    // if accept had errors

    if (new_socket < 0) {
        perror("tcplogger: accept");
        return false;
    }

    // start new service

    tcplogger_service* new_service = malloc(sizeof(tcplogger_service));
    init_tcplogger_service(new_service, new_socket);

    return true;
}

void tcplogger_start(unsigned int requested_port)
{
    int socketfd;

    fprintf(FDD_ACTIVE_LOGFILE, "starting TCP-logger\n");

    if ((socketfd =fdu_listen_inet4(requested_port, FDU_SOCKET_LOCAL)) < 0) {
        fprintf(FDD_ACTIVE_LOGFILE, "TCP-logger FAILED to start\n");
        return;
    }

    // start the service

    {
        fdd_service_input* listening_service = malloc(sizeof(fdd_service_input));
        fdd_init_service_input(listening_service, listening_service, &tcplogger_new_connection);

        fdd_add_input(listening_service, socketfd);
    }
}
