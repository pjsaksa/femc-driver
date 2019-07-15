/* Femc Driver
 * Copyright (C) 2016-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_CAN_HEADER
#define FEMC_DRIVER_CAN_HEADER

#include "can.fwd.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    FDU_CAN_EXT_ID_BIT        = 0x80000000U,
    FDU_CAN_EXT_BYTE_BIT      = 0x80U,
    //
    FDU_CAN_ID_STD_VALUE_MASK = 0x07FFU,
    FDU_CAN_ID_EXT_VALUE_MASK = 0x1FFFFFFFU,
};

typedef uint32_t fdu_can_id_t;

struct fdu_can_frame_t_ {
    fdu_can_id_t id;
    uint8_t size;
    uint8_t data[8];
};

bool fdu_s11n_read_can_bin(fdu_can_frame_t*, const unsigned char**, const unsigned char*);
bool fdu_s11n_write_can_bin(const fdu_can_frame_t*, unsigned char**, const unsigned char*);

bool fdu_s11n_read_can_txt(fdu_can_frame_t*, const unsigned char**, const unsigned char*);
bool fdu_s11n_write_can_txt(const fdu_can_frame_t*, unsigned char**, const unsigned char*, bool);

#endif
