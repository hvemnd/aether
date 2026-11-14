/* SPDX-License-Identifier: GPL-3.0-only
 * AETHER//SYNTAX: tests of ax_learn.c (the curriculum, the scheduler, the tutor), with simulated learners.
 *   cc -std=c99 -Wall -Wextra -Wno-unused-function -Ibuild/gen -Ifirmware/src -o build/host/ax_learn_test \
 *      tests/ax_learn_test.c && build/host/ax_learn_test
 */
#include <stdio.h>
#include <string.h>
#include "ax_core.c"
#include "ax_learn.c"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static ax_learn_t L;

static void test_curriculum(void)
{
    int l, i, k, seen_ear_before_play = 0;
    ax_learn_init(&L);
    CHECK(AXL_N == 36, "36 lessons (%d)", AXL_N);
    CHECK(L.ncards <= AXL_MAXCARDS, "the cards fit (%d of %d)", L.ncards, AXL_MAXCARDS);
    printf("curriculum: %d lessons, %d cards\n", AXL_N, L.ncards);
    for (l = 0; l < AXL_N; l++) {
        CHECK(AXL[l].n <= AXL_MAXNEW, "lesson %d: %d new cards", l + 1, AXL[l].n);
        CHECK(AXL[l].n || (AXL[l].flags & AXL_FINAL), "lesson %d teaches something", l + 1);
        CHECK(strlen(AXL[l].what) <= 34, "lesson %d: its line fits the band", l + 1);
        for (i = 0; i < AXL[l].n; i++) {
            uint16_t c = AXL[l].card[i];
            int kind = AXC_KIND(c), x = AXC_IDX(c);
            CHECK(kind < CK_N, "lesson %d: card kind", l + 1);
            if (kind == CK_NOTE)
                CHECK(x < 12, "note index");
            if (kind == CK_IVL || kind == CK_IVL_EAR)
                CHECK(x >= 1 && x <= 12, "interval size");
            if (kind == CK_CHORD || kind == CK_CHORD_EAR)
                CHECK(x < AX_CHORDS_N, "chord index");
            if (kind == CK_FIELD || kind == CK_FIELD_EAR || kind == CK_MISSING || kind == CK_MORPH)
                CHECK(x < AX_FIELDS_N - 1, "field index (no chromatic)");
            /* hearing a chord type comes after playing it */
            if (kind == CK_CHORD_EAR) {
                int p = -1;
                for (k = 0; k < L.ncards; k++)
                    if (L.card[k] == AXC(CK_CHORD, x))
                        p = L.first[k];
                seen_ear_before_play |= p < 0 || p > l;
            }
        }
    }
    CHECK(!seen_ear_before_play, "every chord is played before it is heard");
    /* everything in the catalog the curriculum means to teach: every chord, every non-chromatic field, every
     * interval, every note */
    {
        int chords = 0, fields = 0, notes = 0, ivls = 0;
        for (k = 0; k < L.ncards; k++) {
            int kind = AXC_KIND(L.card[k]);
            chords += kind == CK_CHORD;
            fields += kind == CK_FIELD;
            notes += kind == CK_NOTE;
            ivls += kind == CK_IVL;
        }
        CHECK(chords == AX_CHORDS_N, "every chord played (%d)", chords);
        CHECK(fields == AX_FIELDS_N - 1, "every field played (%d)", fields);
        CHECK(notes == 12 && ivls == 12, "every note and interval (%d, %d)", notes, ivls);
    }
}

/* a simulated learner: perfect, or one that confuses two answers, or one that misses a card always */
typedef struct { int miss_card; int confuse_card, confuse_with; int err_pct; int run_all; } learner_t;
static int play_lesson(int lesson, const learner_t *who, uint32_t *rng, int max_prompts, int *taught)
{
    int n, teach, i;
    ax_learn_start(&L, lesson, rng);
    *taught = 0;
    for (n = 0; n < max_prompts; n++) {
        i = ax_learn_next(&L, &teach, rng);
        CHECK(i >= 0 && i < L.ncards && ax_learn_in_pool(&L, i), "lesson %d: a card in reach", lesson + 1);
        if (teach) {
            CHECK(ax_learn_is_new(&L, i) && L.s[i].state == AXS_NEW, "only this lesson's new cards are taught");
            ax_learn_taught(&L, i);
            (*taught)++;
            continue;
        }
        CHECK(L.s[i].state != AXS_NEW, "a card asked has been met");
        {
            ax_prompt_t p;
            int right = 1, other = -1;
            ax_learn_prompt(&L, i, &p, rng);
            CHECK(p.target != 0, "a prompt has a target (card %04X)", L.card[i]);
            if (p.name_kind != 0xFF) {
                int j, has = 0;
                CHECK(p.nopt >= 2 && p.nopt <= 12, "2..12 names offered (%d)", p.nopt);
                for (j = 0; j < p.nopt; j++)
                    has |= ax_judge_name(&p, p.opt[j]) == AXJ_RIGHT;
                CHECK(has, "the right name is offered (card %04X)", L.card[i]);
            }
            if (i == who->miss_card)
                right = 0;
            if (i == who->confuse_card) {
                right = 0;
                other = who->confuse_with;
            }
            if (who->err_pct && (int)(ax_rand(rng) % 100u) < who->err_pct)
                right = 0;
            ax_learn_answer(&L, i, right, right ? 2500u : 6000u, 0, other);
        }
        if (ax_learn_mastered(&L) && !who->run_all)
            return n + 1;
    }
    return -1;
}

static void test_perfect_learner(void)
{
    uint32_t rng = 42;
    learner_t perfect = {-1, -1, -1, 0, 0};
    int l, taught, total = 0, worst = 0;
    ax_learn_init(&L);
    for (l = 0; l < AXL_N; l++) {
        int n = play_lesson(l, &perfect, &rng, 400, &taught);
        CHECK(n > 0, "a perfect learner masters lesson %d", l + 1);
        CHECK(taught == AXL[l].n, "lesson %d teaches all %d new cards (%d)", l + 1, AXL[l].n, taught);
        total += n;
        worst = n > worst ? n : worst;
    }
    printf("perfect learner: all %d lessons in %d prompts (longest lesson %d)\n", AXL_N, total, worst);
    CHECK(worst <= 80, "no lesson drags on (%d prompts)", worst);
}

static void test_jump_in(void)
{
    uint32_t rng = 7;
    learner_t perfect = {-1, -1, -1, 0, 0};
    int taught, n, i, presumed = 0, asked_old = 0;
    ax_learn_init(&L);
    n = play_lesson(19, &perfect, &rng, 400, &taught);   /* straight to lesson 20 */
    CHECK(n > 0, "jumping to lesson 20 still masters it");
    for (i = 0; i < L.ncards; i++) {
        presumed += L.first[i] < 19 && L.s[i].state == AXS_PRESUMED;
        asked_old += L.first[i] < 19 && L.s[i].asked;
        CHECK(L.first[i] <= 19 || L.s[i].state == AXS_NEW, "nothing from a later lesson is touched");
    }
    CHECK(asked_old > 0, "earlier lessons are reviewed (%d cards asked)", asked_old);
    printf("jump to lesson 20: mastered in %d prompts, %d earlier cards reviewed, %d still presumed\n", n, asked_old,
           presumed);
}

static void test_lessons_out_of_order(void)
{
    uint32_t rng = 3;
    learner_t perfect = {-1, -1, -1, 0, 0};
    int taught, n;
    ax_learn_init(&L);
    CHECK(play_lesson(8, &perfect, &rng, 400, &taught) > 0, "lesson 9 first");
    n = play_lesson(6, &perfect, &rng, 400, &taught);   /* then back to lesson 7: its card was presumed by lesson 9 */
    CHECK(n >= 1 && n <= 12, "going back: lesson 7 asked again and mastered (n %d, taught %d)", n, taught);
}

static void test_tutor(void)
{
    uint32_t rng = 99;
    int taught, i, maj = -1, mn = -1, w[3], k, c, o, found = 0;
    learner_t confused;
    ax_learn_init(&L);
    for (i = 0; i < L.ncards; i++) {
        if (L.card[i] == AXC(CK_CHORD_EAR, 0))
            maj = i;
        if (L.card[i] == AXC(CK_CHORD_EAR, 1))
            mn = i;
    }
    confused.miss_card = -1;
    confused.confuse_card = mn;                  /* hears minor as major, every time */
    confused.confuse_with = maj;
    confused.err_pct = 0;
    confused.run_all = 0;
    CHECK(play_lesson(8, &confused, &rng, 120, &taught) < 0, "a learner who always mixes them up does not master it");
    k = ax_learn_weakest(&L, w, 3);
    for (i = 0; i < k; i++)
        found |= w[i] == mn;
    CHECK(k > 0 && w[0] == mn, "the tutor's first card to work on: minor by ear (got %d)", k ? w[0] : -1);
    CHECK(ax_learn_confusion(&L, &c, &o) && c == mn && o == maj, "the tutor knows minor is taken for major");
    {
        char b[48];
        ax_learn_card_name(b, sizeof b, L.card[w[0]]);
        printf("tutor: work on \"%s\"\n", b);
    }
    /* a missed card comes back within two prompts */
    {
        int teach, n;
        learner_t perfect = {-1, -1, -1, 0, 0};
        ax_learn_init(&L);
        play_lesson(7, &perfect, &rng, 400, &taught);   /* lesson 8 learned */
        ax_learn_start(&L, 7, &rng);
        i = ax_learn_next(&L, &teach, &rng);
        ax_learn_answer(&L, i, 0, 5000, 0, -1);
        for (n = 0; n < 3; n++) {
            int j = ax_learn_next(&L, &teach, &rng);
            if (j == i)
                break;
            ax_learn_answer(&L, j, 1, 2000, 0, -1);
        }
        CHECK(n < 3, "a miss is asked again within three prompts (%d)", n);
    }
    /* weak earlier cards suggest going back */
    {
        learner_t forgot = {-1, -1, -1, 0, 1};   /* (keeps practising after the lesson is mastered) */
        int e = -1;
        ax_learn_init(&L);
        for (i = 0; i < L.ncards; i++)
            if (L.card[i] == AXC(CK_IVL, 3))
                e = i;                           /* the minor third, from lesson 5 */
        forgot.miss_card = e;
        play_lesson(9, &forgot, &rng, 80, &taught);
        CHECK(ax_learn_suggest_back(&L) == 4, "the tutor suggests lesson 5 again (%d)", ax_learn_suggest_back(&L) + 1);
    }
    /* a 20 % error learner still gets through the early lessons */
    {
        learner_t sloppy = {-1, -1, -1, 20, 0};
        int l, ok = 0;
        ax_learn_init(&L);
        for (l = 0; l < 12; l++)
            ok += play_lesson(l, &sloppy, &rng, 400, &taught) > 0;
        CHECK(ok >= 10, "a learner at 80 %% masters most early lessons (%d of 12)", ok);
        printf("80%% learner: %d of 12 early lessons mastered\n", ok);
    }
}

static void test_tips(void)
{
    char b[64];
    int l, i;
    ax_learn_degrees(b, sizeof b, 9);
    CHECK(!strcmp(b, "1 3 5 b7"), "dom7 degrees: %s", b);
    ax_learn_degrees(b, sizeof b, 11);
    CHECK(!strcmp(b, "1 b3 b5 b7"), "m7b5 degrees: %s", b);
    ax_learn_degrees(b, sizeof b, 21);
    CHECK(!strcmp(b, "1 #9 3 5 b7"), "7#9 degrees: %s", b);
    ax_learn_degrees(b, sizeof b, 6);
    CHECK(!strcmp(b, "1 3 5 6"), "6 degrees: %s", b);
    ax_learn_degrees(b, sizeof b, 22);
    CHECK(!strcmp(b, "1 3 #11 5 7"), "maj7#11 degrees: %s", b);
    ax_learn_tip(b, sizeof b, AXC(CK_MORPH, 1));
    CHECK(!strcmp(b, "change only: b3 b7 "), "Ionian -> Dorian: %s", b);
    for (l = 0; l < AXL_N; l++)
        for (i = 0; i < AXL[l].n; i++) {
            ax_learn_tip(b, sizeof b, AXL[l].card[i]);
            CHECK(b[0] && strlen(b) <= 34, "lesson %d card %d: a tip that fits (%s)", l + 1, i, b);
            ax_learn_card_name(b, sizeof b, AXL[l].card[i]);
            CHECK(b[0], "a name");
        }
}

int main(void)
{
    test_curriculum();
    test_perfect_learner();
    test_jump_in();
    test_lessons_out_of_order();
    test_tutor();
    test_tips();
    printf("ax_learn: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
