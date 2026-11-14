/* SPDX-License-Identifier: GPL-3.0-only
 * AETHER//SYNTAX: tests of ax_core.c (the Lattice's masks, recogniser, laws, prompts, judging, runs).
 *   cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Ibuild/gen -Ifirmware/src -o build/host/ax_core_test \
 *      tests/ax_core_test.c && build/host/ax_core_test
 */
#include <stdio.h>
#include <string.h>
#include "ax_core.c"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t pcs(const int *p, int n)
{
    uint16_t m = 0;
    int i;
    for (i = 0; i < n; i++)
        m |= (uint16_t)(1u << (p[i] % 12));
    return m;
}
static int key_of_note(int midi) { return midi - 53; }   /* F3 = key 0 */
static int find_chord(const char *sym)
{
    int i;
    for (i = 0; i < AX_CHORDS_N; i++)
        if (!strcmp(AX_CHORDS[i].sym, sym))
            return i;
    return -1;
}
static int find_field(const char *name)
{
    int i;
    for (i = 0; i < AX_FIELDS_N; i++)
        if (!strcmp(AX_FIELDS[i].name, name))
            return i;
    return -1;
}

static void test_masks(void)
{
    int c_maj[] = {0, 4, 7}, d_maj[] = {2, 6, 9}, c_min[] = {0, 3, 7};
    CHECK(ax_rot(pcs(c_maj, 3), 2) == pcs(d_maj, 3), "rotation: C major up 2 is D major");
    CHECK(ax_rot(pcs(c_maj, 3), -10) == pcs(d_maj, 3), "rotation wraps either way");
    CHECK(ax_rot(0xFFF, 5) == 0xFFF, "the Divine All is every rotation's");
    /* the Law of Reflection: C E G mirrored about G (C - E - G read downward from G) is G Eb C: C minor */
    CHECK(ax_mirror(pcs(c_maj, 3), 7) != pcs(c_maj, 3), "mirror changes a major triad");
    {
        uint16_t m = ax_mirror(pcs(c_maj, 3), 0);   /* C E G about C: C Ab F = F minor */
        int f_min[] = {5, 8, 0};
        CHECK(m == pcs(f_min, 3), "mirror about C: C major -> F minor (got %03X)", m);
        CHECK(ax_mirror(m, 0) == pcs(c_maj, 3), "a mirror twice is the identity");
    }
    CHECK(ax_popc(0xFFF) == 12 && ax_popc(0) == 0 && ax_popc(0x091) == 3, "popcount");
    /* keys: F3 is key 0 (MIDI 53, pitch class 5), C4 is key 7 */
    CHECK(AX_KEY_PC(0) == 5 && AX_KEY_PC(7) == 0 && AX_KEY_PC(26) == 7, "key pitch classes F3..G5");
    CHECK(ax_pcs_of_keys((1u << 7) | (1u << 11) | (1u << 14)) == pcs(c_maj, 3), "keys C4 E4 G4 -> C major");
    CHECK(ax_pcs_of_keys((1u << 7) | (1u << 19)) == 1u, "two octaves of C fold onto one node");
    (void)c_min;
    /* the fifths face: C G D A E B are neighbours 0..5 */
    CHECK(ax_face_place(7, 1) == 1 && ax_face_place(2, 1) == 2 && ax_face_place(11, 1) == 5, "fifths face places");
}

static void test_catalog(void)
{
    int i, j, n;
    CHECK(AX_PART_N == 12 && AX_BONDS_N == 6 && AX_FIELDS_N == 17 && AX_CHORDS_N == 25, "catalog sizes");
    for (i = 0; i < AX_FIELDS_N; i++) {
        CHECK(AX_FIELDS[i].mask & 1u, "%s holds its root", AX_FIELDS[i].name);
        CHECK(ax_popc(AX_FIELDS[i].mask) == AX_FIELDS[i].n, "%s: n", AX_FIELDS[i].name);
    }
    for (i = 0; i < AX_CHORDS_N; i++)
        for (j = i + 1; j < AX_CHORDS_N; j++)
            CHECK(AX_CHORDS[i].mask != AX_CHORDS[j].mask, "%s / %s distinct", AX_CHORDS[i].name, AX_CHORDS[j].name);
    /* fixed points: whole-tone (period 2), diminished (3), augmented triad (4), dim7 (3) */
    CHECK(AX_FIELDS[find_field("Whole-tone")].period == 2, "whole-tone is a fixed point of period 2");
    CHECK(AX_FIELDS[find_field("Dim half-whole")].period == 3, "diminished: period 3");
    CHECK(AX_CHORDS[find_chord("aug")].period == 4, "augmented triad: period 4");
    CHECK(AX_CHORDS[find_chord("dim7")].period == 3, "dim7: period 3");
    CHECK(AX_FIELDS[find_field("Ionian")].period == 12, "Ionian: no symmetry");
    /* the charge of the major scale: <2 5 4 3 6 1> */
    {
        const ax_obj_t *o = &AX_FIELDS[find_field("Ionian")];
        static const uint8_t W[6] = {2, 5, 4, 3, 6, 1};
        CHECK(!memcmp(o->ivec, W, 6), "Ionian's interval vector");
    }
    (void)ax_table(AX_FIELD, &n);
    CHECK(n == AX_FIELDS_N, "ax_table");
    CHECK(!strcmp(ax_note_name(3), "E\x81"), "E flat's name uses the flat glyph");
}

static void test_recognize(void)
{
    ax_match_t m[8];
    int k;
    int c6[] = {0, 4, 7, 9};
    int cmaj7_drop[] = {4, 11, 0, 7};
    int bogus[] = {0, 1, 2};
    /* twins: C E G A is C6 with C in the bass, Am7 with A in the bass */
    k = ax_recognize(pcs(c6, 4), AX_BOUND, 0, m, 8);
    CHECK(k == 2, "C6 / Am7: two names (got %d)", k);
    CHECK(k >= 1 && m[0].root == 0 && m[0].idx == find_chord("6"), "bass C: C6 first");
    k = ax_recognize(pcs(c6, 4), AX_BOUND, 9, m, 8);
    CHECK(k >= 1 && m[0].root == 9 && m[0].idx == find_chord("m7"), "bass A: Am7 first");
    /* an inversion is the same set */
    k = ax_recognize(pcs(cmaj7_drop, 4), AX_BOUND, 4, m, 8);
    CHECK(k == 1 && m[0].root == 0 && m[0].idx == find_chord("maj7"), "Cmaj7 in any inversion");
    CHECK(ax_recognize(pcs(bogus, 3), AX_BOUND, 0, m, 8) == 0, "a cluster has no name");
    /* fields: C major's notes are Ionian on C, Dorian on D .. seven names */
    k = ax_recognize(AX_FIELDS[find_field("Ionian")].mask, AX_FIELD, 0, m, 8);
    CHECK(k == 7, "the white notes: seven modes (got %d)", k);
    CHECK(m[0].root == 0 && m[0].idx == find_field("Ionian"), ".. C Ionian first with C as the bass");
    /* the augmented triad: three roots, the same set (fixed point) */
    k = ax_recognize(AX_CHORDS[find_chord("aug")].mask, AX_BOUND, -1, m, 8);
    CHECK(k == 3, "C+ = E+ = Ab+ (got %d)", k);
    /* every catalog chord, at every root, in every inversion, is found */
    {
        int i, r, ok = 1;
        for (i = 0; i < AX_CHORDS_N; i++)
            for (r = 0; r < 12; r++) {
                uint16_t abs = ax_rot(AX_CHORDS[i].mask, r);
                int b, found = 0, j;
                for (b = 0; b < 12; b++) {
                    if (!((abs >> b) & 1u))
                        continue;
                    k = ax_recognize(abs, AX_BOUND, b, m, 8);
                    for (j = 0; j < k; j++)
                        found |= m[j].idx == i && m[j].root == r;
                }
                ok &= found;
            }
        CHECK(ok, "every chord at every root, every bass note");
    }
}

static void test_laws(void)
{
    ax_match_t m[64];
    int r, f;
    uint16_t ion = AX_FIELDS[find_field("Ionian")].mask;
    /* the Law of the Anchor: C Ionian's root moved up: D Dorian, then E Phrygian; down from C: B Locrian */
    f = ax_anchor_step(ion, 0, +1, &r);
    CHECK(r == 2 && f == find_field("Dorian"), "anchor up: D Dorian (got %d, %d)", r, f);
    f = ax_anchor_step(ion, 2, +1, &r);
    CHECK(r == 4 && f == find_field("Phrygian"), "anchor up again: E Phrygian");
    f = ax_anchor_step(ion, 0, -1, &r);
    CHECK(r == 11 && f == find_field("Locrian"), "anchor down: B Locrian");
    /* the Law of Containment: C major triad sits in 7-node fields; C Ionian, F Lydian, G Mixolydian among them */
    {
        int i, n = ax_containing_fields(AX_CHORDS[0].mask, m, 64), ion_c = 0, lyd_f = 0, mix_g = 0;
        for (i = 0; i < n && i < 64; i++) {
            ion_c |= m[i].idx == find_field("Ionian") && m[i].root == 0;
            lyd_f |= m[i].idx == find_field("Lydian") && m[i].root == 5;
            mix_g |= m[i].idx == find_field("Mixolydian") && m[i].root == 7;
        }
        CHECK(n > 7 && ion_c && lyd_f && mix_g, "C major inside C Ionian, F Lydian, G Mixolydian (%d fields)", n);
    }
    /* the Law of the Void: the shadow of C major is the black keys, a pentatonic (Gb major pentatonic) */
    {
        int k = ax_recognize((uint16_t)(~ion & AX_ALL), AX_FIELD, 6, m, 8);
        CHECK(k >= 1 && m[0].idx == find_field("Major penta") && m[0].root == 6, "C major's shadow: Gb pentatonic");
    }
    CHECK(ax_bond_class(7) == 5 && ax_bond_class(-5) == 5 && ax_bond_class(6) == 6 && ax_bond_class(12) == 0,
          "bond classes fold inversions");
}

static void test_prompts(void)
{
    uint32_t s = 12345;
    int mode, tier, wave, i;
    for (mode = 0; mode < AXM_ORRERY; mode++)
        for (tier = 1; tier <= 3; tier++)
            for (wave = 0; wave < AX_WAVES; wave++)
                for (i = 0; i < 200; i++) {
                    ax_prompt_t p;
                    ax_prompt_make(&p, mode, tier, wave, &s);
                    CHECK(p.target != 0, "mode %d: a target", mode);
                    if (p.kind == AXP_BOND_PLAY) {
                        int end = p.start_key + p.ivl;
                        CHECK(end >= 0 && end < AX_NKEYS && p.ivl != 0, "a bond stays on the keys (%d %+d)",
                              p.start_key, p.ivl);
                        if (tier == 1)
                            CHECK(p.ivl > 0, "tier 1: bonds go up");
                        if (wave == 0)
                            CHECK(p.ivl <= 2 && p.ivl >= -2, "wave 1: half and whole steps (%d)", p.ivl);
                    }
                    if (p.kind == AXP_NOTE && tier == 1)
                        CHECK((0xAB5u >> p.root) & 1u, "tier 1: natural notes only (%d)", p.root);
                    if (p.kind == AXP_FIELD_MORPH) {
                        CHECK(!(p.target & p.neutral), "morph: changing and shared nodes apart");
                        CHECK(p.target && ax_popc(p.target) <= 7, "morph: something changes");
                    }
                    if (p.kind == AXP_FIELD_MISSING) {
                        CHECK(ax_popc(p.target) == 1 && !(p.target & p.shown), "missing: one dark node");
                        CHECK((p.shown >> p.root) & 1u, "missing: the root stays lit");
                    }
                    if (p.kind == AXP_CHORD_PLAY)
                        CHECK(AX_CHORDS[p.idx].tier <= tier, "tier's chords only");
                    if (p.kind == AXP_FIELD_NAME || p.kind == AXP_FIELD_MISSING || p.kind == AXP_FIELD_MORPH)
                        CHECK(AX_FIELDS[p.kind == AXP_FIELD_MORPH ? p.from : p.idx].n < 12, "no chromatic prompt");
                }
}

static void test_judging(void)
{
    ax_prompt_t p;
    uint16_t lk = 0;
    int c4 = key_of_note(60), e4 = key_of_note(64), g4 = key_of_note(67), eb4 = key_of_note(63), a4 = key_of_note(69);
    /* a note: any octave of the class is right */
    memset(&p, 0, sizeof p);
    p.kind = AXP_NOTE;
    p.target = 1u << 4;
    CHECK(ax_judge_key(&p, e4, 1u << e4, &lk) == AXJ_RIGHT, "E4 for E");
    CHECK(ax_judge_key(&p, e4 + 12, 1u << (e4 + 12), &lk) == AXJ_RIGHT, "E5 for E");
    CHECK(ax_judge_key(&p, c4, 1u << c4, &lk) == AXJ_WRONG, "C4 for E: wrong");
    /* a bond: the start key again is no verdict; the other end exactly */
    memset(&p, 0, sizeof p);
    p.kind = AXP_BOND_PLAY;
    p.start_key = (uint8_t)c4;
    p.ivl = 4;
    p.name_kind = AX_BOND;
    CHECK(ax_judge_key(&p, c4, 1u << c4, &lk) == AXJ_NONE, "bond start again: none");
    CHECK(ax_judge_key(&p, e4, 1u << e4, &lk) == AXJ_RIGHT, "C4 -> E4: major 3rd");
    CHECK(ax_judge_key(&p, e4 + 12, 1u << (e4 + 12), &lk) == AXJ_WRONG, "a 10th is not a 3rd here");
    CHECK(ax_judge_name(&p, 4) == AXJ_RIGHT && ax_judge_name(&p, 3) == AXJ_WRONG, "named: 4 semitones");
    p.ivl = -4;
    CHECK(ax_judge_name(&p, 4) == AXJ_RIGHT, "named: a descending bond by its size");
    /* a chord: build it note by note, any voicing; a stray tone is wrong at once */
    ax_prompt_make(&p, AXM_BOUND, 1, 0, &(uint32_t){7});
    p.kind = AXP_CHORD_PLAY;
    p.root = 0;
    p.target = AX_CHORDS[0].mask;                  /* C major */
    lk = 0;
    CHECK(ax_judge_key(&p, g4, 1u << g4, &lk) == AXJ_NONE, "G alone: not yet");
    CHECK(ax_judge_key(&p, e4, (1u << g4) | (1u << e4), &lk) == AXJ_NONE, "G E: not yet");
    CHECK(lk == ((1u << 7) | (1u << 4)), "locked G and E");
    CHECK(ax_judge_key(&p, c4 + 12, (1u << g4) | (1u << e4) | (1u << (c4 + 12)), &lk) == AXJ_RIGHT,
          "G E C5: C major, inverted");
    lk = 0;
    CHECK(ax_judge_key(&p, eb4, (1u << c4) | (1u << eb4), &lk) == AXJ_WRONG, "C Eb: not C major");
    /* morph: C Ionian -> C Dorian adds Eb and Bb; E is gone (wrong), C shared (none) */
    memset(&p, 0, sizeof p);
    p.kind = AXP_FIELD_MORPH;
    p.root = 0;
    p.from = (uint8_t)find_field("Ionian");
    p.idx = (uint8_t)find_field("Dorian");
    p.shown = AX_FIELDS[p.from].mask;
    p.target = (uint16_t)(AX_FIELDS[p.idx].mask & ~p.shown);
    p.neutral = (uint16_t)(AX_FIELDS[p.idx].mask & p.shown);
    CHECK(p.target == ((1u << 3) | (1u << 10)), "Ionian -> Dorian: b3 and b7");
    lk = 0;
    CHECK(ax_judge_key(&p, c4, 1u << c4, &lk) == AXJ_NONE, "morph: a shared node, none");
    CHECK(ax_judge_key(&p, e4, 1u << e4, &lk) == AXJ_WRONG, "morph: E left the field");
    lk = 0;
    CHECK(ax_judge_key(&p, eb4, 1u << eb4, &lk) == AXJ_NONE, "morph: Eb locked");
    CHECK(ax_judge_key(&p, a4 + 1, 1u << (a4 + 1), &lk) == AXJ_RIGHT, "morph: Bb completes it");
    /* naming a field: twins by set count too (Ionian on C has the same nodes as Aeolian on A, but the root differs) */
    memset(&p, 0, sizeof p);
    p.name_kind = AX_FIELD;
    p.root = 0;
    p.target = AX_FIELDS[find_field("Ionian")].mask;
    CHECK(ax_judge_name(&p, find_field("Ionian")) == AXJ_RIGHT, "named Ionian");
    CHECK(ax_judge_name(&p, find_field("Aeolian")) == AXJ_WRONG, "not Aeolian on C");
}

static void test_runs(void)
{
    ax_run_t r;
    int i, f, waves = 0, ends = 0;
    ax_run_start(&r, AXM_PARTICLE, 1, 99);
    for (i = 0; i < AX_WAVES * AX_PER_WAVE; i++) {
        f = ax_run_answer(&r, AXJ_RIGHT, 0);
        waves += !!(f & AXR_WAVE_CLEAR);
        ends += !!(f & AXR_RUN_END);
    }
    CHECK(waves == 4 && ends == 1 && !r.on, "4 waves of 8, one end (waves %d)", waves);
    CHECK(r.layers == AX_LAYERS && r.right == 32 && r.wrong == 0, "a perfect run builds all six layers");
    CHECK(r.stage == 5, "a perfect run reaches the icosahedron (stage %d)", r.stage);
    ax_run_start(&r, AXM_BOND, 2, 5);
    for (i = 0; i < 3; i++)
        ax_run_answer(&r, AXJ_RIGHT, 0);
    CHECK(r.layers == 3 && r.stage == 3, "three right: three layers, the tetrahedron");
    f = ax_run_answer(&r, AXJ_WRONG, 0);
    CHECK(r.layers == 2 && r.stage == 2 && (f & AXR_STAGE_DOWN), "a miss drops a layer and a form");
    ax_run_answer(&r, AXJ_RIGHT, 1);
    CHECK(r.layers == 2 && r.hinted == 1, "a hinted answer counts, builds nothing");
    /* the same seed asks the same questions */
    {
        ax_run_t a, b;
        int same = 1;
        ax_run_start(&a, AXM_BOUND, 3, 777);
        ax_run_start(&b, AXM_BOUND, 3, 777);
        for (i = 0; i < 20; i++) {
            same &= a.p.target == b.p.target && a.p.idx == b.p.idx;
            ax_run_answer(&a, AXJ_RIGHT, 0);
            ax_run_answer(&b, AXJ_RIGHT, 0);
        }
        CHECK(same, "deterministic prompts");
    }
}

int main(void)
{
    test_masks();
    test_catalog();
    test_recognize();
    test_laws();
    test_prompts();
    test_judging();
    test_runs();
    printf("ax_core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
