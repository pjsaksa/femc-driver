/* Femc Driver
 * Copyright (C) 2015-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

enum { BufferSize =256 };

static int do_name_lookup(const char *name, char *output)
{
    struct sockaddr_in addr;
    struct hostent *he;

    if (strlen(name) < 3)
        return -2;

    if (!(he =gethostbyname(name)))
        return h_errno;

    // DNS was somewhat successful

    if (he->h_addrtype != AF_INET || he->h_length != 4)
        return -1;

    memcpy(&addr.sin_addr, he->h_addr_list[0], 4);

    strcpy(output, inet_ntoa(addr.sin_addr));
    strcat(output, "\n");

    return 0;
}

static int write_output(const char *buffer)
{
    const int len =strlen(buffer);

    for (int written =0; written < len; )
    {
        const int i =write(STDOUT_FILENO, &buffer[written], len-written);

        if (i <= 0) {
            if (errno != EPIPE)
                perror("dns-service:write");
            return -1;
        }

        written +=i;
    }

    return 0;
}

int main(void)
{
    char input_buffer[BufferSize];
    int i, ilen =0;
    char output_buffer[BufferSize];
    char *output_ptr;
    char *inptr;

    for (;;)
    {
        if (BufferSize == ilen)
            ilen =0;

        i =read(STDIN_FILENO, &input_buffer[ilen], BufferSize-ilen);
        if (!i) break;
        else if (i < 0) {
            perror("dns-service:read");
            return 1;
        }
        ilen +=i;

        //

        while ((inptr =memchr(input_buffer, '\n', ilen)))
        {
            *inptr =0;

            if (do_name_lookup(input_buffer, output_buffer) == 0)
                output_ptr =output_buffer;
            else
                output_ptr ="\n";

            //

            if (write_output(output_ptr) != 0)
                return 2;

            //

            ++inptr;
            ilen -=(inptr - input_buffer);
            if (ilen)
                memmove(input_buffer, inptr, ilen);
        }
    }

    return 0;
}
