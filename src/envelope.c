/*
 * envelope.c - parse, authenticate and admit a reconstructed message envelope
 * (SPEC 5).
 *
 * Layout (big-endian integers):
 *   [hdr:1][counter:4][len:2][data:len][tag:8][crc16:2]
 *   signed region = hdr .. data  (7 + len bytes), covered by the SipHash tag
 *   crc16 covers  = hdr .. tag   (15 + len bytes)
 *
 * On acceptance this module performs the durable side effects the spec
 * requires: it stores the new counter and marks the device provisioned. The
 * caller then collects the payload with earshot_take().
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "earshot_internal.h"

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8  | (uint32_t)p[3];
}

earshot_reject esh_envelope_check(const uint8_t *env, size_t env_len,
                                  const earshot_hooks *h, int presence,
                                  const uint8_t **out_data, int *out_len,
                                  uint8_t *keyid, uint32_t *counter)
{
    if (env_len < ESH_ENV_OVERHEAD + 1)
        return EARSHOT_REJ_CRC;                       /* too short: bad decode  */

    uint8_t  ver = (uint8_t)(env[0] >> 4);
    uint8_t  kid = (uint8_t)(env[0] & 0x0F);
    if (ver != EARSHOT_VERSION)
        return EARSHOT_REJ_VERSION;

    uint32_t ctr = be32(env + ESH_ENV_HDR);
    size_t   len = be16(env + ESH_ENV_HDR + ESH_ENV_COUNTER);

    if (len < 1 || ESH_ENV_PREFIX + len + ESH_ENV_SUFFIX > env_len)
        return EARSHOT_REJ_CRC;                       /* length vs decode: bad  */

    size_t signed_len = ESH_ENV_PREFIX + len;
    const uint8_t *tag = env + signed_len;
    const uint8_t *crc = env + signed_len + ESH_ENV_TAG;

    if (esh_crc16(env, signed_len + ESH_ENV_TAG) != be16(crc))
        return EARSHOT_REJ_CRC;

    uint8_t key[EARSHOT_KEY_BYTES];
    if (!h->get_key(h->ctx, kid, key))
        return EARSHOT_REJ_KEYID;

    uint64_t mac = esh_siphash24(key, env, signed_len);
    int ok = esh_tag_equal(tag, mac);
    memset(key, 0, sizeof key);
    if (!ok)
        return EARSHOT_REJ_TAG;

    /* --- SPEC 5.4 acceptance rule --------------------------------------- */

    if (h->locked && h->locked(h->ctx))
        return EARSHOT_REJ_LOCKED;

    uint32_t stored = h->counter_load(h->ctx, kid);
    if (ctr <= stored)
        return EARSHOT_REJ_REPLAY;

    int button = (h->button_recent && h->button_recent(h->ctx)) ? 1 : 0;

    if (stored == 0) {
        if (!presence && !button)
            return EARSHOT_REJ_NEEDS_PRESENCE;
    } else if (ctr - stored > EARSHOT_JUMP_DELTA) {
        if (!button)
            return EARSHOT_REJ_NEEDS_BUTTON;
    }

    /* accepted: durable side effects, then hand the payload back */
    h->counter_store(h->ctx, kid, ctr);
    if (h->mark_provisioned)
        h->mark_provisioned(h->ctx);

    *out_data = env + ESH_ENV_PREFIX;
    *out_len  = (int)len;
    *keyid    = kid;
    *counter  = ctr;
    return EARSHOT_OK;
}
