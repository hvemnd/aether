/* SPDX-License-Identifier: GPL-3.0-only
 * AETHER//SYNTAX: the audio side (the audio ISR's, after cf_out.c's queue, metronome and the sequencer's drums).
 *
 * While AETHER//SYNTAX is open the chord player is silent (the UI sends it no keys) and this plays instead:
 *   - the keys as plain notes, at once, on the chord part's sound (an instrument is never quantized);
 *   - prompts: a bond or a field played to the player, on the next sixteenth, on the same sound;
 *   - the groove, six layers built by right answers: kick, hats, bass (part 1), backbeat, arpeggio and a lead
 *     motif of the player's last right notes (part 2), all from the field on screen, on a sixteenth grid;
 *   - feedback hits, unpitched: drums only, so no interval but the one being taught is ever heard.
 * The beat it keeps is the screen's clock too: the UI reads axa_steps and quantizes its effects to it.
 * Events come through cf_post(CFE_AX, sub-op, b, v). */

enum {
    AXA_ENTER,       /* AETHER//SYNTAX opened: everything else stops */
    AXA_LEAVE,       /* .. closed: its notes go */
    AXA_NOTE,        /* b: MIDI note, v: velocity (0 = up) */
    AXA_GROOVE,      /* b: layers 0..6, v: level 0..127 */
    AXA_FIELD,       /* b: root pitch class, v: the field's absolute mask (the groove's notes) */
    AXA_TEMPO,       /* v: BPM */
    AXA_HIT,         /* b: drum lane, v: level 0..100 */
    AXA_PROMPT_CLR,  /* a new prompt to play: its notes follow */
    AXA_PROMPT_ADD,  /* b: MIDI note, v: 1 harmonic (sounds with the one before) */
    AXA_PROMPT_GO,   /* .. play it from the next sixteenth; v: sixteenths per note */
    AXA_MOTIF,       /* b: a right note for the lead's motif */
    AXA_STOP_ALL,    /* every note of AETHER's parts let go */
    AXA_PULSE,       /* b: a quiet heartbeat on every beat while no layer is up (a challenge's start) */
};

#define AXA_P_CHORD 0u                           /* the parts (cf_sound.c's CF_PART_*, which come later) */
#define AXA_P_BASS 1u
#define AXA_P_LAYER 2u
#define AXA_KIT 12u                              /* SQ_KITS: "X Tech" */
#define AXA_PMAX 16u
static struct {
    uint8_t on;                                  /* AETHER//SYNTAX is open */
    uint8_t layers, level;
    uint16_t field;                              /* the groove's notes (absolute mask) */
    uint8_t root;
    uint16_t bpm;
    uint32_t acc;                                /* samples x bpm x 4 since the last sixteenth */
    uint16_t doff;                               /* drum lanes struck on the last sixteenth (let go on the next) */
    uint8_t bass, arp, lead;                     /* the notes sounding (0 none) */
    uint8_t arp_i, lead_i;
    uint8_t motif[4], nmotif;
    /* the prompt */
    uint8_t pn, pnote[AXA_PMAX], pharm[AXA_PMAX];
    uint8_t pi, pgap, pwait, pon, phold;         /* next note, sixteenths a note, till the next, playing, held */
    uint8_t psound[AXA_PMAX], pns;               /* .. the notes sounding */
    uint8_t pend;                                /* sixteenths left before the last notes are let go */
    uint8_t pulse;                               /* the heartbeat (AXA_PULSE) */
} axa = {.level = 96, .field = 0xAB5, .bpm = 120, .pgap = 2};
static volatile uint32_t axa_steps;              /* sixteenths since AETHER opened (the UI's clock) */
static volatile uint32_t axa_step_ms;            /* .. the engine clock at the last one */
static volatile uint8_t axa_prompt_busy;         /* a prompt is being played (the UI waits to judge) */

static void axa_drum(uint32_t lane, uint32_t lvl)
{
    lane %= SQ_NLANE;
    if (lvl > 100u)
        lvl = 100u;
    sqp_lane_kit(lane, AXA_KIT, lvl);
    trk_note_on(&trk[CF_PART_DRUM], SQ_LANE_NOTE[lane], 100);
    axa.doff |= (uint16_t)(1u << lane);
}
static void axa_off(uint32_t part, uint8_t *n)
{
    if (*n) {
        trk_note_off(&trk[part], *n);
        *n = 0;
    }
}
static void axa_prompt_release(void)
{
    uint32_t i;
    for (i = 0; i < axa.pns; i++)
        trk_note_off(&trk[AXA_P_CHORD], axa.psound[i]);
    axa.pns = 0;
}
static void axa_all_off(void)
{
    uint32_t l;
    axa_off(AXA_P_BASS, &axa.bass);
    axa_off(AXA_P_LAYER, &axa.arp);
    axa_off(AXA_P_LAYER, &axa.lead);
    axa_prompt_release();
    axa.pon = 0;
    axa_prompt_busy = 0;
    for (l = 0; l < SQ_NLANE; l++)
        if ((axa.doff >> l) & 1u)
            trk_note_off(&trk[CF_PART_DRUM], SQ_LANE_NOTE[l]);
    axa.doff = 0;
}
/* the field's notes in order from its root upward, n of them at most */
static uint32_t axa_field_notes(uint8_t *out, uint32_t max)
{
    uint32_t n = 0, k;
    for (k = 0; k < 12u && n < max; k++) {
        uint32_t pc = (axa.root + k) % 12u;
        if ((axa.field >> pc) & 1u)
            out[n++] = (uint8_t)k;               /* (semitones above the root) */
    }
    return n;
}

static void axa_step(void)
{
    uint32_t s = axa_steps & 15u, l, vel, lv = axa.level, duck = axa.pon ? 2u : 1u;
    uint8_t fn[12];
    uint32_t nf;
    for (l = 0; l < SQ_NLANE; l++)               /* last sixteenth's drums let go */
        if ((axa.doff >> l) & 1u)
            trk_note_off(&trk[CF_PART_DRUM], SQ_LANE_NOTE[l]);
    axa.doff = 0;
    axa_off(AXA_P_LAYER, &axa.arp);            /* (an arpeggio note lasts one sixteenth) */
    /* the prompt, first: it is the question */
    if (axa.pon) {
        if (axa.pwait)
            axa.pwait--;
        if (!axa.pwait) {
            if (axa.pi < axa.pn) {
                if (!axa.phold)
                    axa_prompt_release();
                do {
                    trk_note_on(&trk[AXA_P_CHORD], axa.pnote[axa.pi], 104);
                    if (axa.pns < AXA_PMAX)
                        axa.psound[axa.pns++] = axa.pnote[axa.pi];
                    axa.pi++;
                } while (axa.pi < axa.pn && axa.pharm[axa.pi]);
                axa.pwait = axa.pgap;
                axa.pend = (uint8_t)(axa.pgap * 2u);
            } else if (axa.pend && --axa.pend == 0) {
                axa_prompt_release();
                axa.pon = 0;
                axa_prompt_busy = 0;
            } else if (!axa.pend) {
                axa.pon = 0;
                axa_prompt_busy = 0;
            }
        }
    }
    if (!axa.layers && axa.pulse && lv && (s & 3u) == 0u)                  /* the heartbeat: a soft kick a beat */
        axa_drum(0, (s == 0 ? 34u : 22u) * lv / 127u);
    if (!axa.layers || !lv)
        return;
    vel = lv;
    nf = axa_field_notes(fn, 12);
    if (axa.layers >= 1 && (s & 3u) == 0u)                                  /* kick, four on the floor */
        axa_drum(0, (s == 0 ? 96u : 84u) * vel / 127u / duck);
    if (axa.layers >= 2 && (s & 3u) == 2u)                                  /* hats on the off-beats */
        axa_drum(3, 62u * vel / 127u / duck);
    if (axa.layers >= 2 && (s & 1u))                                        /* .. and a quiet sixteenth tick */
        axa_drum(3, 22u * vel / 127u / duck);
    if (axa.layers >= 3 && (s & 1u) == 0u && nf) {                          /* the bass: root, root, fifth, root */
        static const uint8_t PAT[8] = {0, 0, 0, 7, 0, 0, 12, 7};
        uint32_t note = 36u + axa.root + PAT[(s >> 1) & 7u];
        if (!((axa.field >> ((axa.root + 7u) % 12u)) & 1u) && PAT[(s >> 1) & 7u] == 7u)
            note = 36u + axa.root;               /* (no fifth in the field: the root) */
        axa_off(AXA_P_BASS, &axa.bass);
        trk_note_on(&trk[AXA_P_BASS], note, 70u + 30u * vel / 127u);
        axa.bass = (uint8_t)note;
    }
    if (axa.layers >= 4 && (s == 4u || s == 12u))                           /* the backbeat: a clap */
        axa_drum(2, 70u * vel / 127u / duck);
    if (axa.layers >= 5 && !axa.pon && nf) {                                /* the arpeggio through the field */
        uint32_t i = axa.arp_i++ % (nf * 2u), note = 60u + axa.root + (i < nf ? fn[i] : 12u + fn[i - nf]);
        trk_note_on(&trk[AXA_P_LAYER], note, 40u + 40u * vel / 127u);
        axa.arp = (uint8_t)note;
    }
    if (axa.layers >= 6 && !axa.pon && axa.nmotif && (s & 3u) == 0u) {    /* the lead: the player's right notes */
        uint32_t note = axa.motif[axa.lead_i++ % axa.nmotif];
        axa_off(AXA_P_LAYER, &axa.lead);
        while (note < 72u)
            note += 12u;
        trk_note_on(&trk[AXA_P_LAYER], note, 50u + 40u * vel / 127u);
        axa.lead = (uint8_t)note;
    }
}

static void axa_apply(const cf_ev_in_t *e)
{
    switch (e->a) {
    case AXA_ENTER:
        sqp_stop();
        dmp_stop();
        cfp_panic();
        axa.on = 1;
        axa.acc = 0;
        axa.layers = 0;
        axa.pulse = 0;
        axa.nmotif = 0;
        axa_all_off();
        break;
    case AXA_LEAVE:
        axa_all_off();
        trk_all_off(&trk[AXA_P_CHORD]);
        axa.on = 0;
        axa.layers = 0;
        axa.pulse = 0;
        break;
    case AXA_NOTE:
        if (e->v) {
            trk_note_on(&trk[AXA_P_CHORD], e->b, (uint32_t)e->v);
            cfp_midi(0x90u, cfp_ch(AXA_P_CHORD), e->b, (uint32_t)e->v);
        } else {
            trk_note_off(&trk[AXA_P_CHORD], e->b);
            cfp_midi(0x80u, cfp_ch(AXA_P_CHORD), e->b, 0);
        }
        break;
    case AXA_GROOVE:
        if (e->b < axa.layers) {                 /* (layers taken away: their notes go now) */
            if (e->b < 3u)
                axa_off(AXA_P_BASS, &axa.bass);
            if (e->b < 6u)
                axa_off(AXA_P_LAYER, &axa.lead);
        }
        axa.layers = (uint8_t)(e->b > 6u ? 6u : e->b);
        axa.level = (uint8_t)(e->v < 0 ? 0 : e->v > 127 ? 127 : e->v);
        break;
    case AXA_FIELD:
        axa.root = (uint8_t)(e->b % 12u);
        axa.field = (uint16_t)((uint16_t)e->v & 0xFFFu);
        if (!axa.field)
            axa.field = (uint16_t)(1u << axa.root);
        break;
    case AXA_TEMPO: axa.bpm = (uint16_t)(e->v < 60 ? 60 : e->v > 200 ? 200 : e->v); break;
    case AXA_HIT: axa_drum(e->b, (uint32_t)e->v); break;
    case AXA_PROMPT_CLR:
        axa_prompt_release();
        axa.pn = axa.pi = 0;
        axa.pon = 0;
        break;
    case AXA_PROMPT_ADD:
        if (axa.pn < AXA_PMAX) {
            axa.pnote[axa.pn] = e->b;
            axa.pharm[axa.pn] = (uint8_t)(e->v != 0);
            axa.pn++;
        }
        break;
    case AXA_PROMPT_GO:
        axa.pi = 0;
        axa.pgap = (uint8_t)(e->v < 1 ? 1 : e->v > 16 ? 16 : e->v);
        axa.pwait = 1;                           /* (on the next sixteenth) */
        axa.phold = 0;
        if (axa.pn > 1u && axa.pharm[axa.pn - 1u])
            axa.phold = 1;                       /* (a chord: its notes held together) */
        axa.pon = axa.pn != 0;
        axa_prompt_busy = axa.pon;
        break;
    case AXA_MOTIF:
        if (axa.nmotif < 4u)
            axa.motif[axa.nmotif++] = e->b;
        else {
            axa.motif[0] = axa.motif[1];
            axa.motif[1] = axa.motif[2];
            axa.motif[2] = axa.motif[3];
            axa.motif[3] = e->b;
        }
        break;
    case AXA_STOP_ALL: axa_all_off(); break;
    case AXA_PULSE: axa.pulse = (uint8_t)(e->b != 0); break;
    default: break;
    }
}

/* every audio block (cf_audio_block): the sixteenths */
static void axa_tick(uint32_t n)
{
    uint32_t k;
    if (!axa.on)
        return;
    axa.acc += n * axa.bpm * 4u;
    for (k = 0; k < 4u && axa.acc >= FS * 60u; k++) {
        axa.acc -= FS * 60u;
        axa_steps++;
        axa_step_ms = cf_now_ms;
        axa_step();
    }
}
