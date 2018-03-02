/* Femc Driver
 * Copyright (C) 2016-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "can.h"
#include "error_stack.h"
#include "s11n.h"

#include <string.h>

enum { this_error_context =fdu_context_can };

static bool fdu_s11n_read_can_bin_(fdu_can_frame_t *value, const unsigned char **startp, const unsigned char *end)
{
    const fde_node_t *ectx =0;

    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    if (!value
        || !startp
        || !end
        //
        || !*startp
        || *startp > end)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (end - *startp < 3) {
        fde_push_resource_failure_id(fde_resource_buffer_underflow);
        return false;
    }

    const unsigned char *data =*startp;
    const bool extended =(data[0] & FDU_CAN_EXT_BYTE_BIT);

    if (extended
        && end - *startp < 5)
    {
        fde_push_resource_failure_id(fde_resource_buffer_underflow);
        return false;
    }

    const uint8_t size =data[extended ? 4 : 2];

    if (size > 8) {
        fde_push_data_corruption("can frame size over 8");
        return false;
    }

    if (end - *startp < (extended?5:3) + size) {
        fde_push_resource_failure_id(fde_resource_buffer_underflow);
        return false;
    }

    // id

    if (extended) {
        uint32_t tmp_id;

        if (!fdu_s11n_read_uint32(&tmp_id, &data, end))
            return false;

        value->id =(tmp_id & FDU_CAN_ID_EXT_VALUE_MASK) | FDU_CAN_EXT_ID_BIT;
    }
    else {
        uint16_t tmp_id;

        if (!fdu_s11n_read_uint16(&tmp_id, &data, end))
            return false;

        value->id =(tmp_id & FDU_CAN_ID_STD_VALUE_MASK);
    }

    // size

    value->size =size;
    ++data;

    // data

    if (value->size) {
        memcpy(value->data, data, value->size);
        data +=value->size;
    }

    //
    *startp =data;
    return fde_pop_context(this_error_context, ectx);
}

static bool fdu_s11n_write_can_bin_(const fdu_can_frame_t *value, unsigned char **startp, const unsigned char *end)
{
    if (!value
        || !startp
        || !end
        //
        || !*startp
        || *startp > end
        //
        || value->size > 8)
    {
        fde_push_context(this_error_context);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    const bool extended  =(value->id & FDU_CAN_EXT_ID_BIT);
    const int frame_size =(extended?5:3) + value->size;

    if (end - *startp < frame_size) {
        fde_push_context(this_error_context);
        fde_push_resource_failure_id(fde_resource_buffer_overflow);
        return false;
    }

    if (extended) {
        if (!fdu_s11n_write_uint32(&value->id, startp, end))
            return false;
    }
    else {
        const uint16_t tmp_id =value->id;

        if (!fdu_s11n_write_uint16(&tmp_id, startp, end))
            return false;
    }

    unsigned char *data =*startp;

    *data++ =value->size;

    if (value->size) {
        memcpy(data, value->data, value->size);
        data +=value->size;
    }

    //
    *startp =data;

    return true;
}

bool fdu_s11n_read_can_bin(fdu_can_frame_t *value, const unsigned char **startp, const unsigned char *end)
{
    const fdu_endianness_t old_endianness =fdu_s11n_get_endianness();

    if (old_endianness == fdu_big_endian) {
        return fdu_s11n_read_can_bin_(value, startp, end);
    }
    else {
        fdu_s11n_set_endianness(fdu_big_endian);

        const bool rv =fdu_s11n_read_can_bin_(value, startp, end);

        fdu_s11n_set_endianness(old_endianness);

        return rv;
    }
}

bool fdu_s11n_write_can_bin(const fdu_can_frame_t *value, unsigned char **startp, const unsigned char *end)
{
    const fdu_endianness_t old_endianness =fdu_s11n_get_endianness();

    if (old_endianness == fdu_big_endian) {
        return fdu_s11n_write_can_bin_(value, startp, end);
    }
    else {
        fdu_s11n_set_endianness(fdu_big_endian);

        const bool rv =fdu_s11n_write_can_bin_(value, startp, end);

        fdu_s11n_set_endianness(old_endianness);

        return rv;
    }
}


/*
bool fdu_s11n_read_can_txt(fdu_can_frame_t *value, const unsigned char **startp, const unsigned char *end)
{
    return false;
}
*/

static void write_nibble(unsigned char **startp, unsigned char byte)
{
    const unsigned char nibble =byte & 0x0F;

    switch (nibble) {
    case 0x0 ... 0x9: **startp ='0' + nibble; break;
    case 0xA ... 0xF: **startp ='A' - 0xA + nibble; break;
    default: **startp ='?'; break;
    }
    ++(*startp);
}

static int calc_byte_size(const bool extended, const unsigned char can_data_size, const bool write_null)
{
    int count =(extended?8:4) + 3;

    switch (can_data_size) {
    case 0:
        break;
    default:
        count += (can_data_size-1)*3;
        // fallthrough
    case 1:
        count +=2;
        break;
    }

    if (write_null)
        ++count;

    return count;
}

bool fdu_s11n_write_can_txt(const fdu_can_frame_t *value,
                            unsigned char **startp,
                            const unsigned char *end,
                            bool write_null)
{
    const fde_node_t *ectx =0;

    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    if (!value
        || !startp
        || !end
        //
        || !*startp
        || *startp > end
        //
        || value->size > 8)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    const fdu_can_id_t can_id =value->id & FDU_CAN_ID_EXT_VALUE_MASK;
    const bool extended =value->id & FDU_CAN_EXT_ID_BIT;

    if (end - *startp < calc_byte_size(extended, value->size, write_null)) {
        fde_push_resource_failure_id(fde_resource_buffer_overflow);
        return false;
    }

    if (extended) {
        write_nibble(startp, can_id >> 28);
        write_nibble(startp, can_id >> 24);
        write_nibble(startp, can_id >> 20);
        write_nibble(startp, can_id >> 16);
        write_nibble(startp, can_id >> 12);
        write_nibble(startp, can_id >>  8);
        write_nibble(startp, can_id >>  4);
        write_nibble(startp, can_id      );
    }
    else {
        write_nibble(startp, can_id >> 12);
        write_nibble(startp, can_id >>  8);
        write_nibble(startp, can_id >>  4);
        write_nibble(startp, can_id      );
    }

    *(*startp)++ =' ';
    *(*startp)++ =' ';
    write_nibble(startp, value->size);
    *(*startp)++ =' ';
    *(*startp)++ =' ';

    for (int i=0; i<value->size; ++i)
    {
        if (i) *(*startp)++ =' ';

        write_nibble(startp, value->data[i] >>  4);
        write_nibble(startp, value->data[i]      );
    }

    if (write_null)
        **startp =0;

    return fde_pop_context(this_error_context, ectx);
}
