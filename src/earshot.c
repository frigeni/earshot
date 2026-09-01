/*
 * earshot.c - receiver front end and public API (SPEC 3, 5; spec/PROFILE-A.md).
 *
 * Consumes 48 kHz mono PCM, runs a Goertzel bank on each 20 ms sub-block,
 * recovers the sync and the 12 data bytes of each frame, feeds frames to the
 * fountain decoder, and when the message is complete parses and authenticates
 * the envelope.
 *
 * One symbol carries `lanes` nibbles: two for Profile N (a byte per symbol),
 * one for Profile A (single-lane MFSK). Either way a frame is 24 nibbles.
 *
 * Ported from the pre-release `ultra_rx.c`; the DSP behaviour is unchanged for
 * Profile N, generalised to an explicit profile and translated to English.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <math.h>
#include <string.h>
#include "earshot_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const earshot_profile_t EARSHOT_PROFILE_N = {
    16400, { 16700, 17900 }, 60, 16, 2, 4, 1, 2, 12
};
const earshot_profile_t EARSHOT_PROFILE_A = {
    2300, { 2600, 0 }, 100, 16, 1, 3, 1, 1, 24
};

struct earshot {
    const earshot_hooks *hooks;
    earshot_profile_t   p;
    uint64_t samples;                  /* since init, for the presence window */

    int      bins;                     /* 1 + lanes * tones                   */
    float    coeff[ESH_MAX_BINS];

    float    buf[ESH_SUBBLOCK];
    int      buf_n;

    int      locked_on;                /* 0 = seeking sync, 1 = reading frame  */
    int      sync_prev;                /* sync tone over threshold last sub-block */
    int      sub;                      /* sub-block index within the frame     */
    float    acc[ESH_MAX_LANES][ESH_MAX_TONES];
    float    acc_floor;
    int      acc_n;
    uint8_t  frame[ESH_FRAME_BYTES];
    int      nibble_n;                 /* nibbles collected this frame (0..24) */
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

static float bin_coeff(float hz)
{
    return 2.0f * cosf(2.0f * (float)M_PI * hz / (float)ESH_SR);
}

static void prepare_goertzel(struct earshot *e)
{
    const earshot_profile_t *p = &e->p;
    e->bins = 1 + p->lanes * p->tones;
    e->coeff[0] = bin_coeff((float)p->sync_hz);
    for (int l = 0; l < p->lanes; l++)
        for (int i = 0; i < p->tones; i++)
            e->coeff[1 + l * p->tones + i] =
                bin_coeff((float)(p->lane_hz[l] + p->step_hz * i));
}

static void goertzel(const struct earshot *e, const float *x, int n, float *db)
{
    for (int t = 0; t < e->bins; t++) {
        float c = e->coeff[t], s1 = 0, s2 = 0;
        for (int i = 0; i < n; i++) {
            float s0 = x[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        float pw = s1 * s1 + s2 * s2 - c * s1 * s2;
        db[t] = 10.0f * log10f(pw + 1e-12f);
    }
}

/* Strongest tone of one lane, or -1 if the choice is not clean. */
static int lane_argmax(const struct earshot *e, int lane, int n, float floor_sum)
{
    const float *v = e->acc[lane];
    int best = 0;
    float m1 = -1e9f, m2 = -1e9f;
    for (int i = 0; i < e->p.tones; i++) {
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
    memset(e->acc, 0, sizeof e->acc);
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
    (void)data;                        /* payload is read from e->env in take */
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

static void decode_symbol(struct earshot *e)
{
    if (!e->acc_n) { e->frame_bad = 1; return; }
    for (int l = 0; l < e->p.lanes; l++) {
        int v = lane_argmax(e, l, e->acc_n, e->acc_floor);
        if (v < 0) { e->frame_bad = 1; return; }
        if (e->nibble_n < 2 * ESH_FRAME_BYTES) {
            int b = e->nibble_n >> 1;
            if (e->nibble_n & 1) e->frame[b] |= (uint8_t)(v << 4);
            else                 e->frame[b]  = (uint8_t)v;
            e->nibble_n++;
        }
    }
}

static void process_subblock(struct earshot *e)
{
    float db[ESH_MAX_BINS];
    goertzel(e, e->buf, ESH_SUBBLOCK, db);

    float floor_db = 0;
    for (int i = 1; i < e->bins; i++) floor_db += db[i];
    floor_db /= (float)(e->bins - 1);

    /* Lock on the rising edge of the sync tone, not on any over-threshold
     * sub-block. Without this a transient locks the state machine at a wrong
     * phase that then repeats every frame and never recovers. */
    int sync_now = db[0] > floor_db + ESH_SYNC_DB;
    int sync_edge = sync_now && !e->sync_prev;
    e->sync_prev = sync_now;

    if (!e->locked_on) {
        if (sync_edge) {
            e->locked_on = 1;
            e->sub = 1;                 /* the next sub-block opens symbol 1    */
            e->nibble_n = 0;
            e->frame_bad = 0;
            memset(e->frame, 0, sizeof e->frame);
            clear_accum(e);
        }
        return;
    }

    int sym   = e->sub / e->p.sub_per_symbol;
    int phase = e->sub % e->p.sub_per_symbol;

    if (phase >= e->p.sample_first && phase <= e->p.sample_last) {
        for (int l = 0; l < e->p.lanes; l++)
            for (int i = 0; i < e->p.tones; i++)
                e->acc[l][i] += db[1 + l * e->p.tones + i];
        e->acc_floor += floor_db;
        e->acc_n++;
    }

    if (phase == e->p.sub_per_symbol - 1) {   /* symbol boundary */
        if (sym >= 1)
            decode_symbol(e);
        clear_accum(e);
    }

    e->sub++;
    if (e->sub >= (1 + e->p.data_symbols) * e->p.sub_per_symbol) {
        if (!e->frame_bad && e->nibble_n == 2 * ESH_FRAME_BYTES)
            esh_fountain_push(&e->fnt, e->frame);
        e->locked_on = 0;
        if (e->fnt.complete && e->msg_len < 0)
            finish_message(e);
    }
}

/* ---- public API ------------------------------------------------------- */

void earshot_init_profile(earshot_t *e, const earshot_hooks *hooks,
                          const earshot_profile_t *profile)
{
    memset(e, 0, sizeof *e);
    e->hooks   = hooks;
    e->p       = *profile;
    e->msg_len = -1;
    e->status  = EARSHOT_IDLE;
    e->reject  = EARSHOT_OK;
    prepare_goertzel(e);
}

void earshot_init(earshot_t *e, const earshot_hooks *hooks)
{
    earshot_init_profile(e, hooks, &EARSHOT_PROFILE_N);
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
