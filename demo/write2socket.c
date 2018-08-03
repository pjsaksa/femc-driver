/* Femc Driver
 * Copyright (C) 2015-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include <sys/socket.h>
#include <sys/un.h>

#include <unistd.h>
#include <stdio.h>

#ifndef UNIX_PATH_MAX
enum { UNIX_PATH_MAX    =108 };
#endif

enum { buffer_size =4000 };

int main(int argc, char *argv[])
{
    int fd;
    struct sockaddr_un addr;

    if (argc != 2)
        return 1;

    if ((fd =socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return 2;
    }

    addr.sun_family =AF_UNIX;
    strncpy(addr.sun_path, argv[1], UNIX_PATH_MAX);
    addr.sun_path[UNIX_PATH_MAX-1] =0;

    if (connect(fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un)) < 0) {
        perror("connect");
        return 2;
    }

    for (;;) {
        char buffer[buffer_size];

        const int bytes =read(STDIN_FILENO, buffer, buffer_size);
        if (bytes > 0)
        {
            int written =0;

            while (written<bytes)
            {
                const int i =write(fd, &buffer[written], bytes-written);

                if (i>0) written +=i;
                else {
                    perror("write");
                    return 4;
                }
            }
        }
        else if (!bytes) break;
        else if (bytes < 0) {
            perror("read");
            return 3;
        }
    }

    return 0;
}
