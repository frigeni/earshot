/*
 * decode.c - command-line Earshot receiver, for the test bench and as a
 * worked example of the public API.
 *
 * Reads signed 16-bit little-endian 48 kHz mono PCM from a file or stdin,
 * writes the recovered payload to stdout, and prints statistics to stderr.
 *
 * Exit codes: 0 message recovered, 1 nothing recovered,
 *             3 a complete message was rejected (reason on stderr),
 *             2 usage/IO error.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "earshot.h"

static uint8_t  g_key[EARSHOT_KEY_BYTES];
static int      g_have_key = 1;
static uint32_t g_counter  = 0;
static int      g_button   = 1;   /* default: presence satisfied, isolates DSP */
static int      g_locked   = 0;
static int      g_provisioned = 0;

static int cb_get_key(void *ctx, uint8_t keyid, uint8_t key[EARSHOT_KEY_BYTES])
{
    (void)ctx; (void)keyid;
    if (!g_have_key) return 0;
    memcpy(key, g_key, EARSHOT_KEY_BYTES);
    return 1;
}
static uint32_t cb_counter_load(void *ctx, uint8_t keyid)
{
    (void)ctx; (void)keyid; return g_counter;
}
static void cb_counter_store(void *ctx, uint8_t keyid, uint32_t c)
{
    (void)ctx; (void)keyid; g_counter = c;
}
static int  cb_locked(void *ctx) { (void)ctx; return g_locked && g_provisioned; }
static void cb_mark(void *ctx)   { (void)ctx; g_provisioned = 1; }
static int  cb_button(void *ctx) { (void)ctx; return g_button; }

static void parse_hex(const char *h, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        unsigned v = 0;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    const earshot_profile_t *profile = &EARSHOT_PROFILE_N;
    parse_hex("00112233445566778899aabbccddeeff", g_key, EARSHOT_KEY_BYTES);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--key") && i + 1 < argc)
            parse_hex(argv[++i], g_key, EARSHOT_KEY_BYTES);
        else if (!strcmp(argv[i], "--counter") && i + 1 < argc)
            g_counter = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--profile") && i + 1 < argc)
            profile = (argv[++i][0] == 'a' || argv[i][0] == 'A')
                        ? &EARSHOT_PROFILE_A : &EARSHOT_PROFILE_N;
        else if (!strcmp(argv[i], "--no-button"))
            g_button = 0;
        else if (!strcmp(argv[i], "--locked")) {
            g_locked = 1; g_provisioned = 1;
        }
        else if (!strcmp(argv[i], "--no-key"))
            g_have_key = 0;
        else
            path = argv[i];
    }

    FILE *f = path ? fopen(path, "rb") : stdin;
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 2; }

    earshot_hooks hooks;
    memset(&hooks, 0, sizeof hooks);
    hooks.get_key          = cb_get_key;
    hooks.counter_load     = cb_counter_load;
    hooks.counter_store    = cb_counter_store;
    hooks.locked           = cb_locked;
    hooks.mark_provisioned = cb_mark;
    hooks.button_recent    = cb_button;

    earshot_t *e = malloc(earshot_sizeof());
    if (!e) { fprintf(stderr, "oom\n"); return 2; }
    earshot_init_profile(e, &hooks, profile);

    int16_t buf[4096];
    size_t n;
    int got = 0, rejected = 0;
    earshot_reject last = EARSHOT_OK;

    while ((n = fread(buf, sizeof(int16_t), 4096, f)) > 0) {
        earshot_status st = earshot_feed(e, buf, n);
        if (st == EARSHOT_MESSAGE) { got = 1; break; }
        if (st == EARSHOT_REJECTED) {
            rejected = 1;
            last = earshot_reject_reason(e);
        }
    }
    if (f != stdin) fclose(f);

    fprintf(stderr, "blocks %d/%d - frames ok %d\n",
            earshot_blocks_done(e), earshot_blocks_total(e), earshot_frames_ok(e));

    if (got) {
        uint8_t out[EARSHOT_MAX_PAYLOAD];
        int len = earshot_take(e, out, (int)sizeof out);
        fprintf(stderr, "message: keyid %u counter %u len %d\n",
                (unsigned)earshot_last_keyid(e), (unsigned)earshot_last_counter(e), len);
        if (len < 0) { free(e); return 1; }
        fwrite(out, 1, (size_t)len, stdout);
        free(e);
        return 0;
    }

    free(e);
    if (rejected) {
        fprintf(stderr, "rejected: reason %d\n", (int)last);
        return 3;
    }
    fprintf(stderr, "no message\n");
    return 1;
}
