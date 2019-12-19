/* Femc Driver
 * Copyright (C) 2015-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "udplogger.h"

#include "../generic.h"
#include "../dispatcher.h"
#include "../utils.h"

#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>

#define InputBufferSize 1000

typedef struct
{
    fdd_service_input local_input;

    char input_buffer[InputBufferSize];
    unsigned int input_length;

    enum { usd_NotDone, usd_BeingDone, usd_Done } dns_request_status, input_status;
    char* address;
}
    udplogger_service;

static void init_udplogger_service(udplogger_service* service)
{
    service->input_length = 0;

    service->dns_request_status = usd_NotDone;
    service->input_status = usd_NotDone;
    service->address = 0;
}

static void free_udplogger_service(udplogger_service* service)
{
    if (service->address)
        free(service->address);
    free(service);
}

// ------------------------------------------------------------

static void process_input(udplogger_service* service)   // context
{
    char* nl;
    char* colon;
    char* port_str;
    int port_size, port, i;

    nl = memchr(service->input_buffer, '\n', service->input_length);
    if (!nl) {
        free_udplogger_service(service);
        return;
    }
    colon = memchr(service->input_buffer, ':', nl - service->input_buffer);
    if (!colon) {
        free_udplogger_service(service);
        return;
    }

    port_str = colon +1;
    port_size = nl - port_str;

    if (port_size < 1 || port_size > 5) {
        free_udplogger_service(service);
        return;
    }

    port = 0;
    for (i=0; i<port_size; i++) {
        if (!isdigit(port_str[i])) {
            free_udplogger_service(service);
            return;
        }
        port = (port*10) + port_str[i]-'0';
    }

    if (port < 1 || port > 65535) {
        fprintf(FDD_ACTIVE_LOGFILE, "UDP-logger: invalid port number %d\n", port);
        free_udplogger_service(service);
        return;
    }

    ++nl;

    {
        int fd, rv, len;
        struct sockaddr_in sa;

        memset(&sa, 0, sizeof(struct sockaddr_in));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        if (inet_pton(AF_INET, service->address, &sa.sin_addr) <= 0) {
            fprintf(FDD_ACTIVE_LOGFILE, "UDP-logger: failed to convert IP address ('%s')\n", service->address);
            free_udplogger_service(service);
            return;
        }

        fd = socket(PF_INET, SOCK_DGRAM, 0);
        if (fd<0) {
            fprintf(FDD_ACTIVE_LOGFILE, "UDP-logger: failed to open UDP socket\n");
            free_udplogger_service(service);
            return;
        }

        len = &service->input_buffer[service->input_length] - nl;

        {
            // send 'Date:'-header

            const char* format = "%a, %d %b %Y %H:%M:%S %z";
            time_t now = time(0);
            struct tm* tm = localtime(&now);
            char date[50];

            strftime(date, 50, format, tm);

            len += snprintf(&service->input_buffer[service->input_length],
                            InputBufferSize-len,
                            "Date: %s\n",
                            date);
        }

        if (len >= InputBufferSize) {
            fprintf(FDD_ACTIVE_LOGFILE, "UDP-logger: too large data, %d bytes\n", len);
            free_udplogger_service(service);
            return;
        }

        rv = sendto(fd, nl, len,
                    0,
                    (struct sockaddr*)&sa, sizeof(struct sockaddr_in));

        if (rv < 0)
            fprintf(FDD_ACTIVE_LOGFILE, "UDP-logger: sendto returned %d (%s) instead of expected %d\n",
                    rv, strerror(errno), len);
        else if (rv != len)
            fprintf(FDD_ACTIVE_LOGFILE, "UDP-logger: sendto returned %d instead of expected %d\n", rv, len);

        free_udplogger_service(service);
    }
}

// ------------------------------------------------------------

static void udplogger_set_address(void* service_v,              // context
                                  const char* new_address)      // ip address, or 0
{
    udplogger_service* service = (udplogger_service*) service_v;

    if (!service) return;
    if (service->dns_request_status != usd_BeingDone) return;

    if (service->address) {
        free((char*) service->address);
        service->address = 0;
    }

    if (new_address && *new_address)
        service->address = strdup(new_address);

    service->dns_request_status = usd_Done;

    if (service->input_status == usd_Done) {
        process_input(service);
    }
    else if (service->input_status == usd_NotDone)
        free_udplogger_service(service);
}

// -----

static bool udplogger_read_input(void* service_v, int fd)
{
    udplogger_service* service = (udplogger_service*) service_v;

    // read until no more input (read returns 0)

    const int bytes = read(fd, &service->input_buffer[service->input_length], InputBufferSize-service->input_length);

    if (bytes>0) {
        service->input_length += bytes;
        service->input_status = usd_BeingDone;

        if (service->input_length >= InputBufferSize)
        {
            close(fd);
            fdd_remove_input(&service->local_input, fd);

            service->input_status = usd_NotDone;
            if (service->dns_request_status != usd_BeingDone)
                free_udplogger_service(service);
        }

        // start early dns-request in the meanwhile...

        if (service->dns_request_status == usd_NotDone)
        {
            char* colon = 0;

            if ((colon =memchr(service->input_buffer, ':', service->input_length)) != 0)
            {
                *colon = 0;
                if (fdu_dnsserv_lookup(service->input_buffer,
                                       &udplogger_set_address,
                                       service) == 0)
                {
                    service->dns_request_status = usd_BeingDone;
                }
                *colon = ':';
            }
        }

        return true;
    }

    if (bytes<0) {
        if (errno == EINTR || errno == EAGAIN)
            return true;

        perror("udplogger: read");

        service->input_status = usd_NotDone;

        close(fd);
        fdd_remove_input(&service->local_input, fd);

        if (service->dns_request_status != usd_BeingDone)
            free_udplogger_service(service);

        return false;
    }

    // assert: bytes == 0

    service->input_status = usd_Done;
    close(fd);
    fdd_remove_input(&service->local_input, fd);

    //

    if (service->dns_request_status == usd_Done)
        process_input(service);
    else if (service->dns_request_status == usd_NotDone)
        free_udplogger_service(service);

    return true;
}

static bool udplogger_new_connection(void* UNUSED(service), int fd)
{
    int new_socket;

    do {
        new_socket = accept(fd, 0, 0);
    } while (new_socket < 0
             && (errno == EINTR
                 || errno == EAGAIN));

    // if accept had errors

    if (new_socket < 0) {
        perror("udplogger: accept");
        return false;
    }

    // start new service

    printf("UDP-logger: started socket %d\n", new_socket);

    udplogger_service* new_service = malloc(sizeof(udplogger_service));
    init_udplogger_service(new_service);

    fdd_init_service_input(&new_service->local_input,
                           new_service,
                           &udplogger_read_input);
    fdd_add_input(&new_service->local_input, new_socket);

    return true;
}

void udplogger_start(unsigned int requested_port)
{
    int socketfd;

    fprintf(FDD_ACTIVE_LOGFILE, "starting UDP-logger\n");

    if ((socketfd =fdu_listen_inet4(requested_port, FDU_SOCKET_LOCAL)) < 0) {
        fprintf(FDD_ACTIVE_LOGFILE, "UDP-logger FAILED to start\n");
        return;
    }

    // start the service

    {
        fdd_service_input* listening_service = malloc(sizeof(fdd_service_input));
        fdd_init_service_input(listening_service, listening_service, &udplogger_new_connection);

        fdd_add_input(listening_service, socketfd);
    }
}
