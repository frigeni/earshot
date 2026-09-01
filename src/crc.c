/*
 * crc.c - the two CRCs used by Earshot (SPEC 3.4, 5.1).
 *
 *   crc8  : polynomial 0x07, MSB-first, init 0x00, no final XOR.
 *           Guards the 12-byte frame.
 *   crc16 : CRC-16/CCITT-FALSE - polynomial 0x1021, init 0xFFFF,
 *           no reflection, no final XOR. Non-cryptographic integrity check
 *           on the reconstructed envelope.
 *
 * Bit-serial on purpose: no tables, a few bytes of code, identical to the
 * JavaScript and Python implementations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "earshot_internal.h"

uint8_t esh_crc8(const uint8_t *b, size_t n)
{
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= b[i];
        for (int k = 0; k < 8; k++)
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    }
    return c;
}

uint16_t esh_crc16(const uint8_t *b, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)b[i] << 8;
        for (int k = 0; k < 8; k++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}
