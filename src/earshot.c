/*
 * earshot.c - receiver front end and public API (SPEC 3, 5).
 *
 * Consumes 48 kHz mono PCM, runs a 33-bin Goertzel on each 20 ms sub-block,
 * recovers the sync and the 12 data bytes of each frame, feeds frames to the
 * fountain decoder, and when the message is complete parses and authenticates
 * the envelope.
 *
 * Ported from the pre-release `ultra_rx.c`; the DSP path is unchanged in
 * behaviour, translated to English, split from the codec and the crypto.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <math.h>
#include <string.h>
#include "earshot_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ESH_BINS (1 + 2 * ESH_TONES)   /* sync + lane A + lane B = 33 */

struct earshot {
    const earshot_hooks *hooks;
    uint64_t samples;                  /* since init, for the presence window */

    float    coeff[ESH_BINS];          /* Goertzel coefficients (constant)    */

    float    buf[ESH_SUBBLOCK];
    int      buf_n;

    int      locked_on;                /* 0 = seeking sync, 1 = reading frame  */
    int      sync_prev;                /* sync tone over threshold last sub-block */
    int      sub;                      /* sub-block index within the frame     */
    float    acc_a[ESH_TONES];
    float    acc_b[ESH_TONES];
    float    acc_floor;
    int      acc_n;
    uint8_t  frame[ESH_FRAME_BYTES];
    int      frame_n;
    int      frame_bad;

    esh_fountain fnt;

    earshot_status status;
    earshot_reject reject;
    uint8_t  last_keyid;
    uint32_t last_counter;
    int      msg_len;                  /* -1 when nothing is pending           */
    uint8_t  env[ESH_ENV_MAX];         /* assembled envelope / payload source  */
};

size_t earshot_sizeof(void) { return sizeof(struct earshot); }

/* ---- DSP -------------------------------------------------------------- */

static void prepare_goertzel(struct earshot *e)
{
    e->coeff[0] = 2.0f * cosf(2.0f * (float)M_PI * ESH_F_SYNC / ESH_SR);
    for (int i = 0; i < ESH_TONES; i++) {
        e->coeff[1 + i] =
            2.0f * cosf(2.0f * (float)M_PI * (ESH_F_LANE_A + ESH_F_STEP * i) / ESH_SR);
        e->coeff[1 + ESH_TONES + i] =
            2.0f * cosf(2.0f * (float)M_PI * (ESH_F_LANE_B + ESH_F_STEP * i) / ESH_SR);
    }
}

static void goertzel(const struct earshot *e, const float *x, int n, float *db)
{
    for (int t = 0; t < ESH_BINS; t++) {
        float c = e->coeff[t], s1 = 0, s2 = 0;
        for (int i = 0; i < n; i++) {
            float s0 = x[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        float p = s1 * s1 + s2 * s2 - c * s1 * s2;
        db[t] = 10.0f * log10f(p + 1e-12f);
    }
}

/* Strongest lane bin, or -1 if the choice is not clean. */
static int lane_argmax(const float *v, int n, float floor_sum)
{
    int best = 0;
    float m1 = -1e9f, m2 = -1e9f;
    for (int i = 0; i < ESH_TONES; i++) {
        float x = v[i] / n;
        if (x > m1)      { m2 = m1; m1 = x; best = i; }
        else if (x > m2) { m2 = x; }
    }
    if (m1 < floor_sum / n + ESH_TONE_DB) return -1;
    if (m1 < m2 + ESH_MARGIN_DB)          return -1;
    return best;
}

static void clear_accum(struct earshot *e)
{
    memset(e->acc_a, 0, sizeof e->acc_a);
    memset(e->acc_b, 0, sizeof e->acc_b);
    e->acc_floor = 0;
    e->acc_n = 0;
}

static void finish_message(struct earshot *e)
{
    size_t n = esh_fountain_assemble(&e->fnt, e->env);
    if (!n) return;

    const uint8_t *data = NULL;
    int len = 0;
    uint8_t kid = 0;
    uint32_t ctr = 0;
    int presence = (e->samples < ESH_PRESENCE_SAMPLES);

    earshot_reject r = esh_envelope_check(e->env, n, e->hooks, presence,
                                          &data, &len, &kid, &ctr);
    (void)data;                         /* payload is read from e->env in take */
    e->reject = r;
    if (r == EARSHOT_OK) {
        e->last_keyid   = kid;
        e->last_counter = ctr;
        e->msg_len      = len;          /* payload at e->env + ESH_ENV_PREFIX  */
        e->status       = EARSHOT_MESSAGE;
    } else {
        esh_fountain_reset(&e->fnt);    /* bad or inadmissible: decode afresh  */
        e->status = EARSHOT_REJECTED;
    }
}

static void process_subblock(struct earshot *e)
{
    float db[ESH_BINS];
    goertzel(e, e->buf, ESH_SUBBLOCK, db);

    float floor_db = 0;
    for (int i = 0; i < 2 * ESH_TONES; i++) floor_db += db[1 + i];
    floor_db /= (float)(2 * ESH_TONES);

    /* Lock on the rising edge of the sync tone, not on any sub-block that
     * happens to be over threshold. Without this, a transient (a click, a
     * corrupted frame) can lock the state machine at a wrong phase and, because
     * the real sync is still ringing when the misframed frame ends, it re-locks
     * at the same wrong phase every frame and never recovers. */
    int sync_now = db[0] > floor_db + ESH_SYNC_DB;
    int sync_edge = sync_now && !e->sync_prev;
    e->sync_prev = sync_now;

    if (!e->locked_on) {
        if (sync_edge) {
            e->locked_on = 1;
            e->sub = 1;                 /* the next sub-block opens symbol 1    */
            e->frame_n = 0;
            e->frame_bad = 0;
            clear_accum(e);
        }
        return;
    }

    int sym   = e->sub / ESH_SUB_PER_SYM;
    int phase = e->sub % ESH_SUB_PER_SYM;

    if (phase == 1 || phase == 2) {     /* sample the middle of the symbol     */
        for (int i = 0; i < ESH_TONES; i++) {
            e->acc_a[i] += db[1 + i];
            e->acc_b[i] += db[1 + ESH_TONES + i];
        }
        e->acc_floor += floor_db;
        e->acc_n++;
    }

    if (phase == ESH_SUB_PER_SYM - 1) { /* symbol boundary: decode it          */
        if (sym == 0) {                 /* the sync symbol carries no data     */
            clear_accum(e);
            e->sub++;
            return;
        }
        if (e->acc_n) {
            int lo = lane_argmax(e->acc_a, e->acc_n, e->acc_floor);
            int hi = lane_argmax(e->acc_b, e->acc_n, e->acc_floor);
            if (lo < 0 || hi < 0)
                e->frame_bad = 1;
            else if (e->frame_n < ESH_FRAME_BYTES)
                e->frame[e->frame_n++] = (uint8_t)(lo | (hi << 4));
        } else {
            e->frame_bad = 1;
        }
        clear_accum(e);
    }

    e->sub++;
    if (e->sub >= ESH_SYMS_PER_FRM * ESH_SUB_PER_SYM) {
        if (!e->frame_bad && e->frame_n == ESH_FRAME_BYTES)
            esh_fountain_push(&e->fnt, e->frame);
        e->locked_on = 0;
        if (e->fnt.complete && e->msg_len < 0)
            finish_message(e);
    }
}

/* ---- public API ------------------------------------------------------- */

void earshot_init(earshot_t *e, const earshot_hooks *hooks)
{
    memset(e, 0, sizeof *e);
    e->hooks   = hooks;
    e->msg_len = -1;
    e->status  = EARSHOT_IDLE;
    e->reject  = EARSHOT_OK;
    prepare_goertzel(e);
}

earshot_status earshot_feed(earshot_t *e, const int16_t *samples, size_t n)
{
    if (e->status == EARSHOT_MESSAGE)
        return EARSHOT_MESSAGE;             /* caller must earshot_take() first */

    for (size_t i = 0; i < n; i++) {
        e->samples++;
        e->buf[e->buf_n++] = (float)samples[i] / 32768.0f;
        if (e->buf_n == ESH_SUBBLOCK) {
            process_subblock(e);
            e->buf_n = 0;
        }
        if (e->status == EARSHOT_MESSAGE)
            return EARSHOT_MESSAGE;
    }
    return e->status;
}

int earshot_take(earshot_t *e, uint8_t *out, int max)
{
    if (e->status != EARSHOT_MESSAGE || e->msg_len < 0)
        return -1;

    if (max < 0) max = 0;
    int n = e->msg_len;
    if (n > max) n = max;
    memcpy(out, e->env + ESH_ENV_PREFIX, (size_t)n);
    int full = e->msg_len;

    esh_fountain_reset(&e->fnt);
    e->status    = EARSHOT_IDLE;
    e->reject    = EARSHOT_OK;
    e->msg_len   = -1;
    e->locked_on = 0;
    return full;
}

earshot_reject earshot_reject_reason(const earshot_t *e) { return e->reject; }
uint8_t  earshot_last_keyid(const earshot_t *e)   { return e->last_keyid; }
uint32_t earshot_last_counter(const earshot_t *e) { return e->last_counter; }

int earshot_blocks_done(const earshot_t *e)  { return e->fnt.done_count; }
int earshot_blocks_total(const earshot_t *e) { return e->fnt.K; }
int earshot_frames_ok(const earshot_t *e)    { return e->fnt.frames_ok; }
