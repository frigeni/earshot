/*
 * test_unit.c - primitive-level tests: CRCs, SipHash-2-4, tag comparison,
 * and the SPEC 5.4 envelope acceptance rule. The full signal chain is covered
 * by test_e2e.py.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "earshot_internal.h"

static int failures = 0;

#define CHECK(cond, ...) do {                                  \
    if (!(cond)) {                                             \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);            \
        printf(__VA_ARGS__); printf("\n");                     \
        failures++;                                            \
    }                                                          \
} while (0)


/* ---- CRCs -------------------------------------------------------------- */

static void test_crc(void)
{
    /* "123456789" - the classic check string. */
    const uint8_t s[] = { '1','2','3','4','5','6','7','8','9' };

    /* CRC-8/SMBUS: poly 0x07, init 0x00 -> 0xF4 */
    CHECK(esh_crc8(s, 9) == 0xF4, "crc8 check = 0x%02X", esh_crc8(s, 9));

    /* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF -> 0x29B1 */
    CHECK(esh_crc16(s, 9) == 0x29B1, "crc16 check = 0x%04X", esh_crc16(s, 9));

    CHECK(esh_crc8(NULL, 0) == 0x00, "crc8 empty");
    CHECK(esh_crc16(NULL, 0) == 0xFFFF, "crc16 empty");
}


/* ---- SipHash-2-4 ----------------------------------------------------------- */

static void test_siphash(void)
{
    /* Reference vectors: key = 00 01 .. 0f, message = 00 01 .. (len-1). */
    uint8_t key[16];
    for (int i = 0; i < 16; i++) key[i] = (uint8_t)i;

    uint8_t msg[16];
    for (int i = 0; i < 16; i++) msg[i] = (uint8_t)i;

    struct { int len; uint64_t want; } v[] = {
        {  0, 0x726fdb47dd0e0e31ULL },
        {  1, 0x74f839c593dc67fdULL },
        {  2, 0x0d6c8009d9a94f5aULL },
        {  3, 0x85676696d7fb7e2dULL },
        { 15, 0xa129ca6149be45e5ULL },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        uint64_t got = esh_siphash24(key, msg, (size_t)v[i].len);
        CHECK(got == v[i].want, "siphash len %d: got %016llx want %016llx",
              v[i].len, (unsigned long long)got, (unsigned long long)v[i].want);
    }

    /* tag comparison, little-endian serialisation */
    uint64_t mac = 0x0102030405060708ULL;
    uint8_t tag_ok[8]  = { 0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01 };
    uint8_t tag_bad[8] = { 0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x00 };
    CHECK(esh_tag_equal(tag_ok, mac) == 1, "tag_equal match");
    CHECK(esh_tag_equal(tag_bad, mac) == 0, "tag_equal mismatch");
}


/* ---- envelope acceptance rule (SPEC 5.4) --------------------------------- */

static uint8_t   h_key[16];
static int       h_have_key = 1;
static uint32_t  h_stored   = 0;
static int       h_button   = 0;
static int       h_locked   = 0;
static uint32_t  h_last_store;
static int       h_stored_called;
static int       h_marked;

static int hk_get_key(void *c, uint8_t id, uint8_t k[16])
{ (void)c; (void)id; if (!h_have_key) return 0; memcpy(k, h_key, 16); return 1; }
static uint32_t hk_load(void *c, uint8_t id) { (void)c; (void)id; return h_stored; }
static void hk_store(void *c, uint8_t id, uint32_t v)
{ (void)c; (void)id; h_last_store = v; h_stored_called++; }
static int hk_locked(void *c) { (void)c; return h_locked; }
static void hk_mark(void *c) { (void)c; h_marked++; }
static int hk_button(void *c) { (void)c; return h_button; }

static const earshot_hooks HK = {
    hk_get_key, hk_load, hk_store, hk_locked, hk_mark, hk_button, NULL
};

/* Build a valid envelope for keyid 3, given counter and payload. */
static size_t make_env(uint8_t *env, uint32_t counter,
                       const uint8_t *data, size_t len, int ver, int corrupt_crc)
{
    env[0] = (uint8_t)((ver << 4) | 3);
    env[1] = (uint8_t)(counter >> 24);
    env[2] = (uint8_t)(counter >> 16);
    env[3] = (uint8_t)(counter >> 8);
    env[4] = (uint8_t)counter;
    env[5] = (uint8_t)(len >> 8);
    env[6] = (uint8_t)len;
    memcpy(env + 7, data, len);

    size_t signed_len = 7 + len;
    uint64_t mac = esh_siphash24(h_key, env, signed_len);
    for (int i = 0; i < 8; i++)
        env[signed_len + i] = (uint8_t)(mac >> (8 * i));

    uint16_t crc = esh_crc16(env, signed_len + 8);
    if (corrupt_crc) crc ^= 0x0001;
    env[signed_len + 8] = (uint8_t)(crc >> 8);
    env[signed_len + 9] = (uint8_t)crc;

    return signed_len + 10;
}

static earshot_reject run(uint8_t *env, size_t n, int presence)
{
    const uint8_t *d; int l; uint8_t kid; uint32_t ctr;
    h_stored_called = 0; h_marked = 0;
    return esh_envelope_check(env, n, &HK, presence, &d, &l, &kid, &ctr);
}

static void test_acceptance(void)
{
    for (int i = 0; i < 16; i++) h_key[i] = (uint8_t)(0x20 + i);
    const uint8_t data[5] = { 'h','e','l','l','o' };
    uint8_t env[64];

    h_have_key = 1; h_locked = 0; h_button = 0;

    /* normal incremental accept */
    h_stored = 50;
    size_t n = make_env(env, 100, data, sizeof data, EARSHOT_VERSION, 0);
    CHECK(run(env, n, 0) == EARSHOT_OK, "incremental accept");
    CHECK(h_stored_called == 1 && h_last_store == 100, "counter stored on accept");
    CHECK(h_marked == 1, "provisioned marked on accept");

    /* replay / downgrade */
    h_stored = 100;
    CHECK(run(env, n, 1) == EARSHOT_REJ_REPLAY, "equal counter rejected");
    h_stored = 150;
    CHECK(run(env, n, 1) == EARSHOT_REJ_REPLAY, "lower counter rejected");
    CHECK(h_stored_called == 0, "no store on reject");

    /* first provisioning needs presence */
    h_stored = 0;
    CHECK(run(env, n, 0) == EARSHOT_REJ_NEEDS_PRESENCE, "first config, no presence");
    CHECK(run(env, n, 1) == EARSHOT_OK, "first config, power-on window");
    h_button = 1;
    CHECK(run(env, n, 0) == EARSHOT_OK, "first config, button");
    h_button = 0;

    /* forward-jump gate */
    h_stored = 1;
    size_t nj = make_env(env, 1 + EARSHOT_JUMP_DELTA, data, sizeof data, EARSHOT_VERSION, 0);
    CHECK(run(env, nj, 0) == EARSHOT_OK, "jump exactly delta: no gate");
    nj = make_env(env, 2 + EARSHOT_JUMP_DELTA, data, sizeof data, EARSHOT_VERSION, 0);
    CHECK(run(env, nj, 1) == EARSHOT_REJ_NEEDS_BUTTON, "jump > delta needs button");
    h_button = 1;
    CHECK(run(env, nj, 0) == EARSHOT_OK, "jump > delta with button");
    h_button = 0;

    /* lock */
    h_stored = 10; h_locked = 1;
    CHECK(run(env, n, 1) == EARSHOT_REJ_LOCKED, "one-time lock");
    h_locked = 0;

    /* bad version */
    h_stored = 10;
    size_t nv = make_env(env, 100, data, sizeof data, 2, 0);
    CHECK(run(env, nv, 1) == EARSHOT_REJ_VERSION, "bad version");

    /* corrupt crc */
    size_t nc = make_env(env, 100, data, sizeof data, EARSHOT_VERSION, 1);
    CHECK(run(env, nc, 1) == EARSHOT_REJ_CRC, "corrupt crc");

    /* wrong key -> tag mismatch */
    n = make_env(env, 100, data, sizeof data, EARSHOT_VERSION, 0);
    h_key[0] ^= 0xFF;
    CHECK(run(env, n, 1) == EARSHOT_REJ_TAG, "wrong key");
    h_key[0] ^= 0xFF;

    /* no key for this keyid */
    h_have_key = 0;
    CHECK(run(env, n, 1) == EARSHOT_REJ_KEYID, "no key installed");
    h_have_key = 1;
}


int main(void)
{
    test_crc();
    test_siphash();
    test_acceptance();

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all unit tests passed\n");
    return 0;
}
