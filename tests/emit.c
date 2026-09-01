/*
 * emit.c - drive the device-side transmitter and print its symbol stream.
 *
 * Prints one integer per line: -1 for a sync symbol, 0..15 for a data nibble.
 * The Profile A test bench maps these to square-wave tones.
 *
 *   emit --key <hex> --keyid 0 --counter 1 --frames 60 --text "hello"
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "earshot_tx.h"

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
    uint8_t key[16];
    parse_hex("00112233445566778899aabbccddeeff", key, 16);
    uint8_t keyid = 0;
    uint32_t counter = 1;
    int frames = 60;
    const char *text = "device diagnostics over the buzzer";
    uint8_t msg[EARSHOT_TX_MAX_BLOCKS * 8];
    int msglen = -1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--key") && i + 1 < argc)
            parse_hex(argv[++i], key, 16);
        else if (!strcmp(argv[i], "--keyid") && i + 1 < argc)
            keyid = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--counter") && i + 1 < argc)
            counter = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--text") && i + 1 < argc)
            text = argv[++i];
        else if (!strcmp(argv[i], "--hex") && i + 1 < argc) {
            const char *h = argv[++i];
            msglen = (int)strlen(h) / 2;
            parse_hex(h, msg, msglen);
        }
    }

    const uint8_t *data;
    uint16_t len;
    if (msglen >= 0) { data = msg; len = (uint16_t)msglen; }
    else             { data = (const uint8_t *)text; len = (uint16_t)strlen(text); }

    earshot_tx_t tx;
    if (earshot_tx_init(&tx, key, keyid, counter, data, len) != 0) {
        fprintf(stderr, "emit: message length %u out of range\n", len);
        return 2;
    }

    int symbols = frames * (1 + 24);
    for (int i = 0; i < symbols; i++)
        printf("%d\n", earshot_tx_next(&tx));
    return 0;
}
