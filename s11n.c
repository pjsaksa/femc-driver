/* Femc Driver
 * Copyright (C) 2013-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "s11n.h"
#include "error_stack.h"

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

enum { this_error_context = fdu_context_s11n };

// ------------------------------------------------------------

static fdu_endianness_t fdu_endianness = fdu_big_endian;

fdu_endianness_t fdu_s11n_get_endianness(void)
{
    return fdu_endianness;
}

bool fdu_s11n_set_endianness(fdu_endianness_t e)
{
    switch (e) {
    case fdu_big_endian:
    case fdu_little_endian:
        fdu_endianness = e;
        return true;
    }

    fde_push_context(this_error_context);
    fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
    return false;
}

// ------------------------------------------------------------

bool fdu_s11n_read_uint8(uint8_t* value,
                         const unsigned char** datap,
                         const unsigned char* const end)
{
    if (!value
        || !datap
        || !end
        //
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < 1) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_underflow);
        return false;
    }

    const unsigned char* data = *datap;

    *value = data[0];

    ++*datap;
    return true;
}

bool fdu_s11n_read_uint16(uint16_t* value,
                          const unsigned char** datap,
                          const unsigned char* const end)
{
    if (!value
        || !datap
        || !end
        //
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < 2) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_underflow);
        return false;
    }

    const unsigned char* data = *datap;

    switch (fdu_endianness) {
    case fdu_big_endian:
        *value = (((uint16_t)data[0] << 8)
                  |(uint16_t)data[1]);
        break;
    case fdu_little_endian:
        *value = (((uint16_t)data[1] << 8)
                  |(uint16_t)data[0]);
        break;
    }

    *datap += 2;
    return true;
}

bool fdu_s11n_read_uint24(uint32_t* value,
                          const unsigned char** datap,
                          const unsigned char* const end)
{
    if (!value
        || !datap
        || !end
        //
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < 3) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_underflow);
        return false;
    }

    const unsigned char* data = *datap;

    switch (fdu_endianness) {
    case fdu_big_endian:
        *value = (( (uint32_t)data[0] << 16)
                  |((uint32_t)data[1] << 8)
                  | (uint32_t)data[2]);
        break;
    case fdu_little_endian:
        *value = (( (uint32_t)data[2] << 16)
                  |((uint32_t)data[1] << 8)
                  | (uint32_t)data[0]);
        break;
    }

    *datap += 3;
    return true;
}

bool fdu_s11n_read_uint32(uint32_t* value,
                          const unsigned char** datap,
                          const unsigned char* const end)
{
    if (!value
        || !datap
        || !end
        //
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < 4) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_underflow);
        return false;
    }

    const unsigned char* data = *datap;

    switch (fdu_endianness) {
    case fdu_big_endian:
        *value = (( (uint32_t)data[0] << 24)
                  |((uint32_t)data[1] << 16)
                  |((uint32_t)data[2] << 8)
                  | (uint32_t)data[3]);
        break;
    case fdu_little_endian:
        *value = (( (uint32_t)data[3] << 24)
                  |((uint32_t)data[2] << 16)
                  |((uint32_t)data[1] << 8)
                  | (uint32_t)data[0]);
        break;
    }

    *datap += 4;
    return true;
}

bool fdu_s11n_read_uint64(uint64_t* value,
                          const unsigned char** datap,
                          const unsigned char* const end)
{
    if (!value
        || !datap
        || !end
        //
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < 8) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_underflow);
        return false;
    }

    const unsigned char* data = *datap;

    switch (fdu_endianness) {
    case fdu_big_endian:
        *value = (( (uint64_t)data[0] << 56)
                  |((uint64_t)data[1] << 48)
                  |((uint64_t)data[2] << 40)
                  |((uint64_t)data[3] << 32)
                  |((uint64_t)data[4] << 24)
                  |((uint64_t)data[5] << 16)
                  |((uint64_t)data[6] << 8)
                  | (uint64_t)data[7]);
        break;
    case fdu_little_endian:
        *value = (( (uint64_t)data[7] << 56)
                  |((uint64_t)data[6] << 48)
                  |((uint64_t)data[5] << 40)
                  |((uint64_t)data[4] << 32)
                  |((uint64_t)data[3] << 24)
                  |((uint64_t)data[2] << 16)
                  |((uint64_t)data[1] << 8)
                  | (uint64_t)data[0]);
        break;
    }

    *datap += 8;
    return true;
}

bool fdu_s11n_read_float(float* value,
                         const unsigned char** datap,
                         const unsigned char* const end)
{
#if __SIZEOF_FLOAT__ == 4
    return fdu_s11n_read_uint32((uint32_t*)value, datap, end);
#else
#error fdu_s11n_read_float is missing definition
#endif
}

bool fdu_s11n_read_bytes(unsigned char* const value_start,
                         const unsigned char* const value_end,
                         const unsigned char** datap,
                         const unsigned char* const end,
                         bool write_null)
{
    if (!value_start
        || !value_end
        || !datap
        || !end
        //
        || value_start > value_end
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < value_end - value_start) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_underflow);
        return false;
    }

    const unsigned char* data = *datap;
    const uint32_t bytes = value_end - value_start;

    if (bytes) {
        memcpy(value_start, data, bytes);
        *datap += bytes;
    }

    if (write_null)
        value_start[bytes] = 0;

    return true;
}

// ------------------------------------------------------------

bool fdu_s11n_write_uint8(const uint8_t* value,
                          unsigned char** datap,
                          const unsigned char* const end)
{
    if (!value
        || !datap
        || !end
        //
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < 1) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_overflow);
        return false;
    }

    unsigned char* data = *datap;

    data[0] = *value;

    ++*datap;
    return true;
}

bool fdu_s11n_write_uint16(const uint16_t* value,
                           unsigned char** datap,
                           const unsigned char* const end)
{
    if (!value
        || !datap
        || !end
        //
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < 2) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_overflow);
        return false;
    }

    unsigned char* data = *datap;

    switch (fdu_endianness) {
    case fdu_big_endian:
        data[0] = *value >> 8;
        data[1] = *value;
        break;
    case fdu_little_endian:
        data[1] = *value >> 8;
        data[0] = *value;
        break;
    }

    *datap += 2;
    return true;
}

bool fdu_s11n_write_uint24(const uint32_t* value,
                           unsigned char** datap,
                           const unsigned char* const end)
{
    if (!value
        || !datap
        || !end
        //
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < 3) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_overflow);
        return false;
    }

    unsigned char* data = *datap;

    switch (fdu_endianness) {
    case fdu_big_endian:
        data[0] = *value >> 16;
        data[1] = *value >> 8;
        data[2] = *value;
        break;
    case fdu_little_endian:
        data[2] = *value >> 16;
        data[1] = *value >> 8;
        data[0] = *value;
        break;
    }

    *datap += 3;
    return true;
}

bool fdu_s11n_write_uint32(const uint32_t* value,
                           unsigned char** datap,
                           const unsigned char* const end)
{
    if (!value
        || !datap
        || !end
        //
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < 4) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_overflow);
        return false;
    }

    unsigned char* data = *datap;

    switch (fdu_endianness) {
    case fdu_big_endian:
        data[0] = *value >> 24;
        data[1] = *value >> 16;
        data[2] = *value >> 8;
        data[3] = *value;
        break;
    case fdu_little_endian:
        data[3] = *value >> 24;
        data[2] = *value >> 16;
        data[1] = *value >> 8;
        data[0] = *value;
        break;
    }

    *datap += 4;
    return true;
}

bool fdu_s11n_write_uint64(const uint64_t* value,
                           unsigned char** datap,
                           const unsigned char* const end)
{
    if (!value
        || !datap
        || !end
        //
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < 8) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_overflow);
        return false;
    }

    unsigned char* data = *datap;

    switch (fdu_endianness) {
    case fdu_big_endian:
        data[0] = *value >> 56;
        data[1] = *value >> 48;
        data[2] = *value >> 40;
        data[3] = *value >> 32;
        data[4] = *value >> 24;
        data[5] = *value >> 16;
        data[6] = *value >> 8;
        data[7] = *value;
        break;
    case fdu_little_endian:
        data[7] = *value >> 56;
        data[6] = *value >> 48;
        data[5] = *value >> 40;
        data[4] = *value >> 32;
        data[3] = *value >> 24;
        data[2] = *value >> 16;
        data[1] = *value >> 8;
        data[0] = *value;
        break;
    }

    *datap += 8;
    return true;
}

bool fdu_s11n_write_float(const float* value,
                          unsigned char** datap,
                          const unsigned char* const end)
{
#if __SIZEOF_FLOAT__ == 4
    return fdu_s11n_write_uint32((const uint32_t*)value, datap, end);
#else
#error fdu_s11n_write_float is missing definition
#endif
}

bool fdu_s11n_write_bytes(const unsigned char* const value_start,
                          const unsigned char* const value_end,
                          unsigned char** datap,
                          const unsigned char* const end)
{
    if (!value_start
        || !value_end
        || !datap
        || !end
        //
        || value_start > value_end
        || !*datap
        || *datap > end)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *datap < value_end - value_start)
    {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_overflow);
        return false;
    }

    unsigned char* data = *datap;
    const uint32_t bytes = value_end - value_start;

    if (bytes) {
        memcpy(data, value_start, bytes);
        *datap += bytes;
    }

    return true;
}
