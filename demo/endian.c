/* Femc Driver
 * Copyright (C) 2016-2020 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "../generic.h"
#include "../error_stack.h"
#include "../s11n.h"

#include <stdio.h>
#include <string.h>

void print_buffer(const char* label, const unsigned char* buffer)
{
    printf("%-15s: %02X %02X %02X %02X %02X %02X %02X %02X",
           label,
           buffer[0], buffer[1], buffer[2], buffer[3],
           buffer[4], buffer[5], buffer[6], buffer[7]);

    if (memcmp(buffer, "\x01\x02\x03\x04\x05\x06\x07\x08", 8) == 0) {
        printf(" - big-endian\n");
    }
    else if (memcmp(buffer, "\x08\x07\x06\x05\x04\x03\x02\x01", 8) == 0) {
        printf(" - little-endian\n");
    }
    else {
        printf(" - something else -endian\n");
    }
}

int main(void)
{
    const uint64_t num = 0x0102030405060708;
    enum { sizeof_num = sizeof(num) };

    unsigned char buffer[sizeof_num];

    printf("num            : 0x%016"PRINTF_INT64_MOD"X\n", num);

    putchar('\n');

    {
        print_buffer("cast to string", (const unsigned char*) &num);
    }

    {
        memcpy(buffer, &num, sizeof_num);
        print_buffer("memcpy", buffer);
    }

    putchar('\n');

    {
        unsigned char* s = buffer;
        fdu_s11n_write_uint64(&num, &s, buffer + sizeof_num);
        print_buffer("s11n (default)", buffer);
    }

    fdu_s11n_set_endianness(fdu_little_endian);

    {
        unsigned char* s = buffer;
        fdu_s11n_write_uint64(&num, &s, buffer + sizeof_num);
        print_buffer("s11n (le)", buffer);
    }

    fdu_s11n_set_endianness(fdu_big_endian);

    {
        unsigned char* s = buffer;
        fdu_s11n_write_uint64(&num, &s, buffer + sizeof_num);
        print_buffer("s11n (be)", buffer);
    }

    return 0;
}
