/* SPDX-License-Identifier: GPL-3.0-only
 * AETHER//SYNTAX: the Lattice's theory, pure (no HAL, no drawing, no sound): masks, the recogniser, the laws, the
 * CHALLENGE prompts and their judging, a run's waves and its score. Included by ax_ui.c on the unit and in the
 * emulator, and by tests/ax_core_test.c on the host.
 *
 * Every musical object is a 12-bit mask of pitch classes ("nodes"). A catalog entry's mask is written from its root
 * (bit 0); an absolute mask is that rotated up by the root. The keys are F3..G5: key k sounds MIDI note 53 + k. */
#include <stdint.h>
#include "ax_lattice.h"

#define AX_NKEYS 27
#define AX_KEY_NOTE(k) (53 + (k))
#define AX_KEY_PC(k) ((uint8_t)((53 + (k)) % 12))
#define AX_ALL 0xFFFu

/* ---------------------------------------------------------------- masks --- */
static uint16_t ax_rot(uint32_t m, int k)        /* every node moved up k semitones (the Law of Rotation) */
{
    k = ((k % 12) + 12) % 12;
    m &= AX_ALL;
    return (uint16_t)(((m << k) | (m >> (12 - k))) & AX_ALL);
}
static uint16_t ax_mirror(uint32_t m, int axis)  /* reflected about the root `axis` (the Law of Reflection) */
{
    uint16_t r = 0;
    int i;
    for (i = 0; i < 12; i++)
        if ((m >> i) & 1u)
            r |= (uint16_t)(1u << (((2 * axis - i) % 12 + 12) % 12));
    return r;
}
static int ax_popc(uint32_t m)
{
    int n = 0;
    for (m &= AX_ALL; m; m &= m - 1u)
        n++;
    return n;
}
static uint16_t ax_pcs_of_keys(uint32_t keys)
{
    uint16_t m = 0;
    int k;
    for (k = 0; k < AX_NKEYS; k++)
        if ((keys >> k) & 1u)
            m |= (uint16_t)(1u << AX_KEY_PC(k));
    return m;
}
static int ax_lowest_key(uint32_t keys)
{
    int k;
    for (k = 0; k < AX_NKEYS; k++)
        if ((keys >> k) & 1u)
            return k;
    return -1;
}
/* the fifths face: node i of the chromatic ring sits at place (7 i) mod 12, so a diatonic field becomes an arc */
static int ax_face_place(int pc, int fifths) { return fifths ? (pc * 7) % 12 : pc; }

/* ----------------------------------------------------------- the catalog --- */
static const ax_obj_t *ax_table(int kind, int *n)
{
    switch (kind) {
    case AX_PARTICLE: *n = AX_PART_N; return AX_PART;
    case AX_BOND: *n = AX_BONDS_N; return AX_BONDS;
    case AX_FIELD: *n = AX_FIELDS_N; return AX_FIELDS;
    default: *n = AX_CHORDS_N; return AX_CHORDS;
    }
}
static const char *ax_note_name(int pc) { return AX_PART[((pc % 12) + 12) % 12].sym; }

/* directed intervals by semitones, 0..12 */
static const char *const AX_IVL_NAME[13] = {"unison", "minor 2nd", "major 2nd", "minor 3rd", "major 3rd",
                                            "perfect 4th", "tritone", "perfect 5th", "minor 6th", "major 6th",
                                            "minor 7th", "major 7th", "octave"};
static const char *const AX_IVL_SHORT[13] = {"P1", "m2", "M2", "m3", "M3", "P4", "TT", "P5", "m6", "M6", "m7", "M7",
                                             "P8"};
static int ax_bond_class(int semis)              /* 1..6: the line across the ring; 0 a unison or an octave */
{
    int d = ((semis % 12) + 12) % 12;
    return d > 6 ? 12 - d : d;
}

/* ------------------------------------------------------------ recognising --- */
typedef struct {
    uint8_t idx;                 /* catalog index */
    uint8_t root;                /* its root's pitch class */
} ax_match_t;
/* every catalog object of `kind` (AX_FIELD or AX_BOUND) that lights exactly the nodes of `pcs`, at any root it holds.
 * Twins (C6 / Am7) are several answers: the one rooted on `bass_pc` first (-1: table order). Returns how many */
static int ax_recognize(uint16_t pcs, int kind, int bass_pc, ax_match_t *out, int max)
{
    int n, i, r, got = 0, pass;
    const ax_obj_t *t = ax_table(kind, &n);
    if (!pcs)
        return 0;
    for (pass = 0; pass < 2; pass++)
        for (r = 0; r < 12; r++) {
            if (!((pcs >> r) & 1u) || ((r == bass_pc) != (pass == 0)))
                continue;
            for (i = 0; i < n; i++)
                if (ax_rot(t[i].mask, r) == pcs && got < max) {
                    out[got].idx = (uint8_t)i;
                    out[got].root = (uint8_t)r;
                    got++;
                }
        }
    return got;
}
/* a chord's absolute mask inside fields (the Law of Containment): how many (field, root) pairs of the seven-node
 * fields hold it; the first `max` written to out */
static int ax_containing_fields(uint16_t chord, ax_match_t *out, int max)
{
    int i, r, got = 0;
    for (i = 0; i < AX_FIELDS_N; i++) {
        if (AX_FIELDS[i].n != 7)
            continue;
        for (r = 0; r < 12; r++)
            if ((ax_rot(AX_FIELDS[i].mask, r) & chord) == chord) {
                if (got < max) {
                    out[got].idx = (uint8_t)i;
                    out[got].root = (uint8_t)r;
                }
                got++;
            }
    }
    return got;
}

/* ----------------------------------------------------------- the anchor --- */
/* the Law of the Anchor: one polygon, its root moved to the next (dir +1) or previous lit node. The field it becomes
 * is looked up (Ionian -> Dorian -> Phrygian ..); -1 when the catalog has no name for it (a pentatonic's other modes) */
static int ax_anchor_step(uint16_t abs_mask, int root, int dir, int *new_root)
{
    int k, r = root;
    ax_match_t m;
    for (k = 0; k < 12; k++) {
        r = ((r + (dir > 0 ? 1 : -1)) % 12 + 12) % 12;
        if ((abs_mask >> r) & 1u)
            break;
    }
    *new_root = r;
    for (k = 0; k < AX_FIELDS_N; k++)
        if (ax_rot(AX_FIELDS[k].mask, r) == abs_mask) {
            m.idx = (uint8_t)k;
            return m.idx;
        }
    return -1;
}

/* ------------------------------------------------------------------ random --- */
static uint32_t ax_rand(uint32_t *s)             /* xorshift32: seeded, so a test's run is the same every time */
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
static int ax_randn(uint32_t *s, int n) { return n > 0 ? (int)(ax_rand(s) % (uint32_t)n) : 0; }

/* ------------------------------------------------------------- the modes --- */
enum { AXM_PARTICLE, AXM_BOND, AXM_FIELD, AXM_BOUND, AXM_ORRERY, AXM_LEARN, AXM_N };
static const char *const AXM_NAME[AXM_N] = {"PARTICLE", "BOND", "FIELD", "BOUND STATE", "ORRERY", "LEARN"};
static const char *const AXM_WHAT[AXM_N] = {"NOTES", "INTERVALS", "SCALES + MODES", "CHORDS", "ALL TOGETHER",
                                            "LESSONS + REVIEW"};

/* ------------------------------------------------------------ the prompts --- */
enum {
    AXP_NOTE,                    /* a note's name shown: press it */
    AXP_NOTE_SIGIL,              /* .. its sigil only */
    AXP_BOND_PLAY,               /* a bond heard from start_key: play its other end (or name it) */
    AXP_FIELD_MISSING,           /* a field with one node dark: play the missing one */
    AXP_FIELD_NAME,              /* a field heard: name it */
    AXP_FIELD_MORPH,             /* one field to another on the same root: play only the nodes that change */
    AXP_CHORD_PLAY,              /* a chord's symbol: play any voicing */
    AXP_FIELD_PLAY,              /* a field's name: play every one of its nodes, any order, any octave */
    AXP_CHORD_NAME,              /* a chord heard: name its type */
    AXP_BOND_NAME,               /* a bond heard: name it */
};
typedef struct {
    uint8_t kind;                /* AXP_* */
    uint8_t root;                /* the pitch class it is built on */
    uint8_t idx;                 /* catalog index: the chord, the field (MORPH: the target), the particle */
    uint8_t from;                /* MORPH: the source field */
    int8_t ivl;                  /* BOND: semitones from start_key, signed */
    uint8_t start_key;           /* BOND: the key it starts on */
    uint8_t missing;             /* FIELD_MISSING: the dark node's pitch class */
    uint16_t target;             /* the nodes the answer must light (absolute) */
    uint16_t shown;              /* the nodes the sigil shows (absolute) */
    uint16_t neutral;            /* nodes a key may press with no verdict (MORPH: the shared ones) */
    uint8_t name_kind;           /* a prompt that can be named with ALGORITHM: AX_BOND (classes by semitones) or AX_FIELD;
                                  * 0xFF none */
    uint8_t name_ans;            /* .. the right option: semitones (BOND) or a field index */
    uint8_t nopt;                /* the names offered (0: every one of name_kind's); LEARN offers only what was taught */
    uint8_t opt[12];
} ax_prompt_t;

/* the fields a tier asks about (the chromatic never: there is nothing to find in it) */
static int ax_field_ok(int i, int tier) { return AX_FIELDS[i].tier <= tier && AX_FIELDS[i].n < 12; }
static int ax_pick_field(uint32_t *s, int tier, int seven_only)
{
    int i, n = 0, k;
    for (i = 0; i < AX_FIELDS_N; i++)
        n += ax_field_ok(i, tier) && (!seven_only || AX_FIELDS[i].n == 7);
    k = ax_randn(s, n);
    for (i = 0; i < AX_FIELDS_N; i++)
        if (ax_field_ok(i, tier) && (!seven_only || AX_FIELDS[i].n == 7) && k-- == 0)
            return i;
    return 0;
}

/* a new prompt for `mode` at `tier` (1..3) in `wave` (0..3). Later waves ask harder things of the same tier */
static void ax_prompt_make(ax_prompt_t *p, int mode, int tier, int wave, uint32_t *s)
{
    int i, n;
    p->kind = AXP_NOTE;
    p->root = p->idx = p->from = p->start_key = p->missing = 0;
    p->ivl = 0;
    p->target = p->shown = p->neutral = 0;
    p->name_kind = 0xFF;
    p->name_ans = 0;
    p->nopt = 0;
    switch (mode) {
    case AXM_PARTICLE:
    default: {
        static const uint8_t NAT[7] = {0, 2, 4, 5, 7, 9, 11};
        int pc = tier <= 1 ? NAT[ax_randn(s, 7)] : ax_randn(s, 12);
        p->kind = (tier >= 2 && wave >= 2 && ax_randn(s, 2)) ? AXP_NOTE_SIGIL : AXP_NOTE;
        p->root = p->idx = (uint8_t)pc;
        p->target = p->shown = (uint16_t)(1u << pc);
        break;
    }
    case AXM_BOND: {
        static const uint8_t WAVE_MAX[4] = {2, 4, 7, 12};
        int mx = WAVE_MAX[wave & 3], semis, up, start;
        if (tier <= 1 && mx > 7)
            mx = 7;
        semis = 1 + ax_randn(s, mx);
        up = !(tier >= 2 && wave >= 1 && ax_randn(s, 3) == 0);   /* (descending bonds from tier 2's second wave) */
        start = up ? ax_randn(s, AX_NKEYS - semis) : semis + ax_randn(s, AX_NKEYS - semis);
        p->kind = AXP_BOND_PLAY;
        p->ivl = (int8_t)(up ? semis : -semis);
        p->start_key = (uint8_t)start;
        p->root = AX_KEY_PC(start);
        p->target = (uint16_t)(1u << AX_KEY_PC(start + p->ivl));
        p->shown = (uint16_t)((1u << p->root) | p->target);
        p->name_kind = AX_BOND;
        p->name_ans = (uint8_t)semis;
        break;
    }
    case AXM_FIELD: {
        int sub = ax_randn(s, tier <= 1 ? 2 : 3);  /* 0 missing node, 1 morph, 2 name it */
        int r = ax_randn(s, 12), f = ax_pick_field(s, tier, sub == 1);
        uint16_t m = ax_rot(AX_FIELDS[f].mask, r);
        p->root = (uint8_t)r;
        p->idx = (uint8_t)f;
        if (sub == 1) {                          /* MORPH: another seven-node field on the same root */
            int g, tries = 0;
            do {
                g = ax_pick_field(s, tier < 2 ? 2 : tier, 1);
            } while ((g == f || ax_rot(AX_FIELDS[g].mask, r) == m) && ++tries < 16);
            if (g == f)
                g = (f + 1) % 7;
            p->kind = AXP_FIELD_MORPH;
            p->from = (uint8_t)f;
            p->idx = (uint8_t)g;
            p->target = (uint16_t)(ax_rot(AX_FIELDS[g].mask, r) & ~m);   /* the nodes the new field adds */
            p->neutral = (uint16_t)(ax_rot(AX_FIELDS[g].mask, r) & m);   /* .. the ones both share */
            p->shown = m;
        } else if (sub == 2) {
            p->kind = AXP_FIELD_NAME;
            p->target = p->shown = m;
            p->name_kind = AX_FIELD;
            p->name_ans = (uint8_t)f;
        } else {                                 /* MISSING: any node but the root goes dark */
            int k = 1 + ax_randn(s, AX_FIELDS[f].n - 1), pc;
            for (pc = 0, n = 0; pc < 12; pc++)
                if (((AX_FIELDS[f].mask >> pc) & 1u) && n++ == k)
                    break;
            p->kind = AXP_FIELD_MISSING;
            p->missing = (uint8_t)((pc + r) % 12);
            p->target = (uint16_t)(1u << p->missing);
            p->shown = (uint16_t)(m & ~p->target);
        }
        break;
    }
    case AXM_BOUND: {
        int k;
        for (i = 0, n = 0; i < AX_CHORDS_N; i++)
            n += AX_CHORDS[i].tier <= tier;
        k = ax_randn(s, n);
        for (i = 0; i < AX_CHORDS_N; i++)
            if (AX_CHORDS[i].tier <= tier && k-- == 0)
                break;
        p->kind = AXP_CHORD_PLAY;
        p->idx = (uint8_t)i;
        p->root = (uint8_t)ax_randn(s, 12);
        p->target = p->shown = ax_rot(AX_CHORDS[i].mask, p->root);
        break;
    }
    }
}

/* ------------------------------------------------------------- judging --- */
enum { AXJ_NONE, AXJ_RIGHT, AXJ_WRONG };
/* a key went down (key) with `held` the keys down now (that one included); `locked` collects the target's nodes
 * found so far (lock-on). Returns AXJ_* */
static int ax_judge_key(const ax_prompt_t *p, int key, uint32_t held, uint16_t *locked)
{
    uint16_t pcb = (uint16_t)(1u << AX_KEY_PC(key)), hp;
    switch (p->kind) {
    case AXP_NOTE:
    case AXP_NOTE_SIGIL:
    case AXP_FIELD_MISSING:
        if (pcb & p->target)
            return AXJ_RIGHT;
        if (p->kind == AXP_FIELD_MISSING && (pcb & p->shown))
            return AXJ_NONE;                     /* (a node that is there: tried, no verdict) */
        return AXJ_WRONG;
    case AXP_BOND_PLAY:
        if (key == p->start_key)
            return AXJ_NONE;                     /* (the bond's start struck again: heard, no verdict) */
        return key - p->start_key == p->ivl ? AXJ_RIGHT : AXJ_WRONG;
    case AXP_FIELD_MORPH:
        if (pcb & p->neutral)
            return AXJ_NONE;
        if (!(pcb & p->target))
            return AXJ_WRONG;
        *locked |= pcb;
        return (*locked & p->target) == p->target ? AXJ_RIGHT : AXJ_NONE;
    case AXP_CHORD_PLAY:
        hp = ax_pcs_of_keys(held);
        if (hp & ~p->target)
            return AXJ_WRONG;
        *locked = hp;
        return hp == p->target ? AXJ_RIGHT : AXJ_NONE;
    case AXP_FIELD_PLAY:                          /* (struck one by one: each node of the field locks on) */
        if (!(pcb & p->target))
            return AXJ_WRONG;
        *locked |= pcb;
        return (*locked & p->target) == p->target ? AXJ_RIGHT : AXJ_NONE;
    default:
        return AXJ_NONE;
    }
}
/* an answer named with ALGORITHM (opt: semitones for a bond, a field index for a field) */
static int ax_judge_name(const ax_prompt_t *p, int opt)
{
    if (p->name_kind == AX_BOND)
        return opt == (p->ivl < 0 ? -p->ivl : p->ivl) ? AXJ_RIGHT : AXJ_WRONG;
    if (p->name_kind == AX_FIELD)                 /* (two fields with the same nodes from this root: either name) */
        return ax_rot(AX_FIELDS[opt % AX_FIELDS_N].mask, p->root) == p->target ? AXJ_RIGHT : AXJ_WRONG;
    if (p->name_kind == AX_BOUND)
        return ax_rot(AX_CHORDS[opt % AX_CHORDS_N].mask, p->root) == p->target ? AXJ_RIGHT : AXJ_WRONG;
    return AXJ_NONE;
}
/* the options ALGORITHM steps through for a named answer: how many, and option i's label */
static int ax_name_count(const ax_prompt_t *p)
{
    return p->nopt ? p->nopt : p->name_kind == AX_BOND ? 12 : p->name_kind == AX_BOUND ? AX_CHORDS_N : AX_FIELDS_N - 1;
}
static int ax_name_opt(const ax_prompt_t *p, int i)   /* the value option i stands for */
{
    int n = ax_name_count(p);
    i = ((i % n) + n) % n;
    if (p->nopt)
        return p->opt[i];
    return p->name_kind == AX_BOND ? i + 1 : i;
}
static const char *ax_name_label(const ax_prompt_t *p, int v)   /* option value v as words */
{
    if (p->name_kind == AX_BOND)
        return AX_IVL_NAME[v % 13];
    if (p->name_kind == AX_BOUND)
        return AX_CHORDS[v % AX_CHORDS_N].name;
    return AX_FIELDS[v % AX_FIELDS_N].name;
}

/* ---------------------------------------------------------------- a run --- */
#define AX_WAVES 4
#define AX_PER_WAVE 8
#define AX_LAYERS 6
enum { AXR_WAVE_CLEAR = 1, AXR_RUN_END = 2, AXR_STAGE_UP = 4, AXR_STAGE_DOWN = 8, AXR_LAYER_UP = 16 };
typedef struct {
    uint8_t on;                  /* a run is under way */
    uint8_t mode, tier;
    uint8_t wave, n;             /* the wave (0..3), prompts answered in it */
    uint8_t layers;              /* groove layers built (0..6) */
    uint8_t stage;               /* the Ascension form (0..5) */
    uint8_t clean;               /* the wave has had no miss, all six layers up since it began */
    uint8_t right, wrong, hinted;
    uint32_t rng;
    ax_prompt_t p;               /* the prompt being asked */
} ax_run_t;

static int ax_stage_of(int layers, int ascended)
{
    static const uint8_t S[AX_LAYERS + 1] = {0, 1, 2, 3, 3, 4, 4};
    return ascended ? 5 : S[layers < 0 ? 0 : layers > AX_LAYERS ? AX_LAYERS : layers];
}
static void ax_run_start(ax_run_t *r, int mode, int tier, uint32_t seed)
{
    r->on = 1;
    r->mode = (uint8_t)(mode < AXM_ORRERY ? mode : AXM_BOUND);
    r->tier = (uint8_t)(tier < 1 ? 1 : tier > 3 ? 3 : tier);
    r->wave = r->n = 0;
    r->layers = 0;
    r->stage = 0;
    r->clean = 0;
    r->right = r->wrong = r->hinted = 0;
    r->rng = seed ? seed : 1u;
    ax_prompt_make(&r->p, r->mode, r->tier, 0, &r->rng);
}
/* the prompt answered (verdict AXJ_RIGHT or AXJ_WRONG; a timeout is a wrong one); hinted: the hint was used, so a right
 * answer builds no layer. The next prompt is made. Returns AXR_* flags */
static int ax_run_answer(ax_run_t *r, int verdict, int hinted)
{
    int f = 0, st0 = r->stage;
    if (!r->on)
        return 0;
    if (verdict == AXJ_RIGHT) {
        r->right++;
        if (hinted)
            r->hinted++;
        else if (r->layers < AX_LAYERS) {
            r->layers++;
            f |= AXR_LAYER_UP;
        }
    } else {
        r->wrong++;
        r->clean = 0;
        if (r->layers)
            r->layers--;
    }
    if (++r->n >= AX_PER_WAVE) {
        int ascended = r->clean && r->layers == AX_LAYERS;
        r->n = 0;
        f |= AXR_WAVE_CLEAR;
        r->stage = (uint8_t)ax_stage_of(r->layers, ascended || (r->stage == 5 && verdict == AXJ_RIGHT));
        if (++r->wave >= AX_WAVES) {
            r->on = 0;
            f |= AXR_RUN_END;
        }
        r->clean = r->layers == AX_LAYERS;     /* (the next wave counts toward the last form if it begins full) */
    } else {
        r->stage = (uint8_t)ax_stage_of(r->layers, r->stage == 5 && verdict == AXJ_RIGHT);
        if (r->layers == AX_LAYERS && verdict == AXJ_RIGHT && !r->clean && r->n == 1)
            r->clean = 1;
    }
    if (r->stage > st0)
        f |= AXR_STAGE_UP;
    else if (r->stage < st0)
        f |= AXR_STAGE_DOWN;
    if (r->on)
        ax_prompt_make(&r->p, r->mode, r->tier, r->wave, &r->rng);
    return f;
}
