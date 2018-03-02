/* Femc Driver
 * Copyright (C) 2013-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_S11N_HEADER
#define FEMC_DRIVER_S11N_HEADER

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    fdu_big_endian,     // network-byte order, default
    fdu_little_endian,
} fdu_endianness_t;

fdu_endianness_t fdu_s11n_get_endianness(void);
bool fdu_s11n_set_endianness(fdu_endianness_t);

// ***** read

bool fdu_s11n_read_uint8(uint8_t *, const unsigned char **, const unsigned char *);
bool fdu_s11n_read_uint16(uint16_t *, const unsigned char **, const unsigned char *);
bool fdu_s11n_read_uint24(uint32_t *, const unsigned char **, const unsigned char *);
bool fdu_s11n_read_uint32(uint32_t *, const unsigned char **, const unsigned char *);
bool fdu_s11n_read_uint64(uint64_t *, const unsigned char **, const unsigned char *);

bool fdu_s11n_read_float(float *, const unsigned char **, const unsigned char *);
bool fdu_s11n_read_double(double *, const unsigned char **, const unsigned char *);

bool fdu_s11n_read_bytes(unsigned char *, const unsigned char *,        // value start, end
                         const unsigned char **, const unsigned char *, // buffer start, end
                         bool);                                         // write null?

// ***** write

bool fdu_s11n_write_uint8(const uint8_t *, unsigned char **, const unsigned char *);
bool fdu_s11n_write_uint16(const uint16_t *, unsigned char **, const unsigned char *);
bool fdu_s11n_write_uint24(const uint32_t *, unsigned char **, const unsigned char *);
bool fdu_s11n_write_uint32(const uint32_t *, unsigned char **, const unsigned char *);
bool fdu_s11n_write_uint64(const uint64_t *, unsigned char **, const unsigned char *);

bool fdu_s11n_write_float(const float *, unsigned char **, const unsigned char *);
bool fdu_s11n_write_double(const double *, unsigned char **, const unsigned char *);

bool fdu_s11n_write_bytes(const unsigned char *, const unsigned char *,         // value start, end
                          unsigned char **, const unsigned char *);             // buffer start, end

#endif
