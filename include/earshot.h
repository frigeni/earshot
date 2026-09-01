/*
 * earshot.h - portable receiver for the Earshot acoustic configuration channel.
 *
 * See spec/SPEC.md for the wire protocol. This header is the entire public API.
 *
 * Portability: C99. No malloc, no stdio, no threads. The DSP path uses libm
 * (cosf, log10f, sqrtf, logf, log). All state lives in a caller-provided
 * earshot_t; place it statically or on the stack.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EARSHOT_H
#define EARSHOT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol version carried in the envelope header (SPEC 5.1). */
#define EARSHOT_VERSION            1

/* Compile-time limits. No allocation depends on runtime values. */
#define EARSHOT_BLOCK              8
#define EARSHOT_MAX_BLOCKS         255
#define EARSHOT_ENVELOPE_OVERHEAD  17    /* hdr+counter+len+tag+crc16 */
#define EARSHOT_MAX_PAYLOAD        (EARSHOT_MAX_BLOCKS * EARSHOT_BLOCK - EARSHOT_ENVELOPE_OVERHEAD) /* 2023 */
#define EARSHOT_KEY_BYTES          16    /* SipHash-2-4 key */
#define EARSHOT_TAG_BYTES          8

/* Physical-presence window after earshot_init(), in milliseconds (SPEC 5.5).
 * Measured on the 48 kHz sample clock inside earshot_feed(). */
#define EARSHOT_PRESENCE_WINDOW_MS 60000

/* Forward-jump gate: a counter more than this far ahead of the stored value
 * needs the button specifically, not just the power-on window (SPEC 5.4). */
#define EARSHOT_JUMP_DELTA         1024


/* ------------------------------------------------------------------ status - */

typedef enum {
    EARSHOT_IDLE = 0,   /* still collecting; nothing to do */
    EARSHOT_MESSAGE,     /* a complete, authenticated message is ready: call earshot_take() */
    EARSHOT_REJECTED     /* a complete message failed a check; see earshot_reject_reason().
                            The decoder has reset itself and keeps listening. */
} earshot_status;

typedef enum {
    EARSHOT_OK = 0,
    EARSHOT_REJ_VERSION,        /* ver field != EARSHOT_VERSION */
    EARSHOT_REJ_CRC,            /* crc16 mismatch: envelope mis-reconstructed */
    EARSHOT_REJ_TAG,            /* SipHash tag mismatch: wrong key or forgery */
    EARSHOT_REJ_KEYID,          /* no key installed for this keyid */
    EARSHOT_REJ_REPLAY,         /* counter <= stored (replay or downgrade) */
    EARSHOT_REJ_LOCKED,         /* one-time lock: device already provisioned */
    EARSHOT_REJ_NEEDS_PRESENCE, /* first config or forward jump, no presence signal */
    EARSHOT_REJ_NEEDS_BUTTON    /* forward jump > delta: button press required */
} earshot_reject;


/* ------------------------------------------------------------------- hooks - */
/*
 * Platform glue. Every callback gets the opaque `ctx`. All are required except
 * where noted. Keep them fast and non-blocking; they are called from
 * earshot_feed().
 */
typedef struct {
    /* Fill key[] and return 1 if a key is installed for `keyid`, else return 0
     * (which yields EARSHOT_REJ_KEYID). */
    int (*get_key)(void *ctx, uint8_t keyid, uint8_t key[EARSHOT_KEY_BYTES]);

    /* Highest counter ever accepted for `keyid`, or 0 if none. Must be backed
     * by non-volatile storage. */
    uint32_t (*counter_load)(void *ctx, uint8_t keyid);

    /* Persist `counter` for `keyid`. Called only on acceptance. Must be durable
     * against power loss mid-write (two-slot with a sequence tag, SPEC 5.4). */
    void (*counter_store)(void *ctx, uint8_t keyid, uint32_t counter);

    /* Return nonzero if the one-time lock is enabled AND a message has already
     * been accepted since manufacture. Return 0 if the lock is disabled.
     * May be NULL, treated as "always 0". */
    int (*locked)(void *ctx);

    /* Called on the first acceptance, so a later locked() can report true.
     * May be NULL if locked() is NULL. */
    void (*mark_provisioned)(void *ctx);

    /* Return nonzero if a button was pressed within EARSHOT_PRESENCE_WINDOW_MS.
     * Devices with no button return 0; the power-on window still applies for
     * first provisioning. May be NULL, treated as "no button". */
    int (*button_recent)(void *ctx);

    void *ctx;
} earshot_hooks;


/* ------------------------------------------------------------------- state - */
/*
 * Opaque to callers: do not read or write fields. Exposed only so it can be
 * allocated without malloc. Size is dominated by the fountain buffers
 * (~4.5 KB). sizeof(earshot_t) is stable within a protocol version.
 */
typedef struct earshot earshot_t;

/* Size in bytes, for callers that place the state in a provided buffer. */
size_t earshot_sizeof(void);


/* --------------------------------------------------------------------- api - */

/* Initialise for Profile N (near-ultrasonic inbound, SPEC 3.3). `hooks` is
 * borrowed and must outlive `e`. Resets the presence window to now. */
void earshot_init(earshot_t *e, const earshot_hooks *hooks);

/* Feed 48 kHz mono int16 PCM. Returns the current status. When it returns
 * EARSHOT_MESSAGE, stop feeding and call earshot_take(). When it returns
 * EARSHOT_REJECTED, inspect earshot_reject_reason() if you care; the decoder
 * has already reset and will keep going on the next call. */
earshot_status earshot_feed(earshot_t *e, const int16_t *samples, size_t n);

/* Copy the authenticated payload into out[0..max-1]. Returns its length in
 * bytes, or -1 if no authenticated message is pending. Resets the decoder for
 * the next message. */
int earshot_take(earshot_t *e, uint8_t *out, int max);

/* Why the last complete message was rejected (EARSHOT_OK if none was). */
earshot_reject earshot_reject_reason(const earshot_t *e);

/* The keyid and counter of the last accepted message (valid after
 * EARSHOT_MESSAGE, before the next earshot_take()). */
uint8_t  earshot_last_keyid(const earshot_t *e);
uint32_t earshot_last_counter(const earshot_t *e);


/* ---------------------------------------------------------------- progress - */
/* For diagnostics and tests. */

int earshot_blocks_done(const earshot_t *e);   /* source blocks recovered */
int earshot_blocks_total(const earshot_t *e);  /* K, or 0 if not learned yet */
int earshot_frames_ok(const earshot_t *e);     /* frames that passed crc8 */


#ifdef __cplusplus
}
#endif
#endif /* EARSHOT_H */
