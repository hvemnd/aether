/* SPDX-License-Identifier: GPL-3.0-only
 * AETHER//SYNTAX: LEARN, the lessons. A curriculum of 36 lessons from the first three notes to altered chords and
 * symmetric scales; inside a session, spaced repetition and a small tutor that keeps track of what you need to work
 * on. Pure (no HAL): included by ax_ui.c after ax_core.c, and by tests/ax_learn_test.c on the host.
 *
 * A CARD is one thing to know: a note, an interval to play or to hear, a chord to play or to hear, a field to play,
 * hear, complete or morph into. Its root changes every time it is asked, so the card is the concept, not one chord.
 * Lesson L teaches its own new cards and reviews every card of the lessons before it. Nothing is kept over a
 * power-off: the lesson number is chosen with ALGORITHM, and the cards of the lessons before it start as "presumed
 * known", asked now and then; a miss there tells the tutor they are not.
 *
 * The scheduler counts prompts, not days (a session is minutes long): a card in box b comes back after AXL_GAP[b]
 * prompts. Right moves it up a box (two for a quick first answer from box 0); right with the hint, or slow, keeps
 * it; wrong drops it to box 0 and asks it again two prompts later. The tutor ranks cards by misses, accuracy and
 * slowness, and remembers which two answers you mix up. */

/* ---------------------------------------------------------------- cards --- */
enum { CK_NOTE, CK_IVL, CK_IVL_EAR, CK_CHORD, CK_CHORD_EAR, CK_FIELD, CK_FIELD_EAR, CK_MISSING, CK_MORPH, CK_N };
#define AXC(k, i) ((uint16_t)((k) << 8 | (i)))
#define AXC_KIND(c) ((int)((c) >> 8))
#define AXC_IDX(c) ((int)((c) & 0xFFu))
static const char *const CK_VERB[CK_N] = {"FIND", "PLAY", "HEAR", "PLAY", "HEAR", "PLAY", "HEAR", "COMPLETE", "MORPH"};

/* ---------------------------------------------------------------- lessons --- */
enum { AXL_EASYROOT = 1, AXL_DESC = 2, AXL_FINAL = 4 };
#define AXL_MAXNEW 8
typedef struct {
    const char *title;           /* the band's big line (2x when it fits) */
    const char *what;            /* .. its second */
    uint8_t flags;               /* AXL_* */
    uint8_t n;
    uint16_t card[AXL_MAXNEW];   /* the cards it teaches */
} ax_lesson_t;

static const ax_lesson_t AXL[] = {
    {"C D E", "the first three white keys", AXL_EASYROOT, 3, {AXC(CK_NOTE, 0), AXC(CK_NOTE, 2), AXC(CK_NOTE, 4)}},
    {"F G A B", "the rest of the white keys", AXL_EASYROOT, 4,
     {AXC(CK_NOTE, 5), AXC(CK_NOTE, 7), AXC(CK_NOTE, 9), AXC(CK_NOTE, 11)}},
    {"BLACK KEYS", "sharps and flats", AXL_EASYROOT, 5,
     {AXC(CK_NOTE, 1), AXC(CK_NOTE, 3), AXC(CK_NOTE, 6), AXC(CK_NOTE, 8), AXC(CK_NOTE, 10)}},
    {"STEPS", "half steps and whole steps", AXL_EASYROOT, 2, {AXC(CK_IVL, 1), AXC(CK_IVL, 2)}},
    {"THIRDS", "minor and major thirds", AXL_EASYROOT, 2, {AXC(CK_IVL, 3), AXC(CK_IVL, 4)}},
    {"4THS + 5THS", "the perfect intervals", AXL_EASYROOT, 2, {AXC(CK_IVL, 5), AXC(CK_IVL, 7)}},
    {"MAJOR SCALE", "seven notes: W W H W W W H", AXL_EASYROOT, 1, {AXC(CK_FIELD, 0)}},
    {"TRIADS", "major and minor chords", AXL_EASYROOT, 2, {AXC(CK_CHORD, 0), AXC(CK_CHORD, 1)}},
    {"HEAR TRIADS", "major or minor, by ear", AXL_EASYROOT, 2, {AXC(CK_CHORD_EAR, 0), AXC(CK_CHORD_EAR, 1)}},
    {"6THS + 8VE", "sixths and the octave", AXL_EASYROOT, 3, {AXC(CK_IVL, 8), AXC(CK_IVL, 9), AXC(CK_IVL, 12)}},
    {"HEAR STEPS", "seconds and thirds, by ear", AXL_EASYROOT, 4,
     {AXC(CK_IVL_EAR, 1), AXC(CK_IVL_EAR, 2), AXC(CK_IVL_EAR, 3), AXC(CK_IVL_EAR, 4)}},
    {"MINOR SCALE", "the natural minor; major or minor?", AXL_EASYROOT, 3,
     {AXC(CK_FIELD, 5), AXC(CK_FIELD_EAR, 0), AXC(CK_FIELD_EAR, 5)}},
    {"PENTATONIC", "the five-note scales", AXL_EASYROOT, 2, {AXC(CK_FIELD, 9), AXC(CK_FIELD, 10)}},
    {"DIM + AUG", "diminished and augmented triads", AXL_EASYROOT, 2, {AXC(CK_CHORD, 2), AXC(CK_CHORD, 3)}},
    {"SUS CHORDS", "sus2 and sus4: no third", AXL_EASYROOT, 2, {AXC(CK_CHORD, 4), AXC(CK_CHORD, 5)}},
    {"HEAR 4 TRIADS", "dim, aug, sus2, sus4 by ear", AXL_EASYROOT, 4,
     {AXC(CK_CHORD_EAR, 2), AXC(CK_CHORD_EAR, 3), AXC(CK_CHORD_EAR, 4), AXC(CK_CHORD_EAR, 5)}},
    {"7THS + TRITONE", "minor 7th, major 7th, tritone", 0, 3, {AXC(CK_IVL, 6), AXC(CK_IVL, 10), AXC(CK_IVL, 11)}},
    {"HEAR BONDS", "4ths to octaves, by ear", 0, 8,
     {AXC(CK_IVL_EAR, 5), AXC(CK_IVL_EAR, 6), AXC(CK_IVL_EAR, 7), AXC(CK_IVL_EAR, 8), AXC(CK_IVL_EAR, 9),
      AXC(CK_IVL_EAR, 10), AXC(CK_IVL_EAR, 11), AXC(CK_IVL_EAR, 12)}},
    {"MAJ7 + DOM7", "major 7th and dominant 7th", 0, 2, {AXC(CK_CHORD, 8), AXC(CK_CHORD, 9)}},
    {"MIN7 + M7b5", "minor 7th and half-diminished", 0, 2, {AXC(CK_CHORD, 10), AXC(CK_CHORD, 11)}},
    {"DIM7 + mMAJ7", "diminished 7th, minor-major 7th", 0, 2, {AXC(CK_CHORD, 12), AXC(CK_CHORD, 13)}},
    {"HEAR 7THS", "four seventh chords by ear", 0, 4,
     {AXC(CK_CHORD_EAR, 8), AXC(CK_CHORD_EAR, 9), AXC(CK_CHORD_EAR, 10), AXC(CK_CHORD_EAR, 11)}},
    {"DORIAN + MIXO", "the first two modes", 0, 2, {AXC(CK_FIELD, 1), AXC(CK_FIELD, 4)}},
    {"LYDIAN + PHRYG", "a bright mode and a dark one", 0, 2, {AXC(CK_FIELD, 3), AXC(CK_FIELD, 2)}},
    {"LOCRIAN", "the last mode; find missing nodes", 0, 4,
     {AXC(CK_FIELD, 6), AXC(CK_MISSING, 0), AXC(CK_MISSING, 1), AXC(CK_MISSING, 4)}},
    {"MORPH", "turn major into each mode", 0, 4,
     {AXC(CK_MORPH, 1), AXC(CK_MORPH, 3), AXC(CK_MORPH, 4), AXC(CK_MORPH, 5)}},
    {"HEAR MODES", "the modes, by ear", 0, 5,
     {AXC(CK_FIELD_EAR, 1), AXC(CK_FIELD_EAR, 2), AXC(CK_FIELD_EAR, 3), AXC(CK_FIELD_EAR, 4), AXC(CK_FIELD_EAR, 6)}},
    {"BLUES + HARM", "blues scale, harmonic minor", 0, 2, {AXC(CK_FIELD, 11), AXC(CK_FIELD, 7)}},
    {"MELODIC MINOR", "and the three minors by ear", 0, 3,
     {AXC(CK_FIELD, 8), AXC(CK_FIELD_EAR, 7), AXC(CK_FIELD_EAR, 8)}},
    {"6THS + ADD9", "major 6, minor 6, add 9", AXL_DESC, 3,
     {AXC(CK_CHORD, 6), AXC(CK_CHORD, 7), AXC(CK_CHORD, 16)}},
    {"NINTHS", "major 9, dominant 9, minor 9", AXL_DESC, 3,
     {AXC(CK_CHORD, 17), AXC(CK_CHORD, 18), AXC(CK_CHORD, 19)}},
    {"ALTERED", "7b9 and 7#9", AXL_DESC, 2, {AXC(CK_CHORD, 20), AXC(CK_CHORD, 21)}},
    {"COLOUR CHORDS", "maj7#11, 7sus4, maj7#5", AXL_DESC, 3,
     {AXC(CK_CHORD, 22), AXC(CK_CHORD, 15), AXC(CK_CHORD, 14)}},
    {"11THS + 13THS", "minor 11, dominant 13", AXL_DESC, 2, {AXC(CK_CHORD, 23), AXC(CK_CHORD, 24)}},
    {"SYMMETRIC", "whole-tone, diminished, augmented", AXL_DESC, 4,
     {AXC(CK_FIELD, 12), AXC(CK_FIELD, 13), AXC(CK_FIELD, 14), AXC(CK_FIELD, 15)}},
    {"FINAL", "everything, every direction", AXL_DESC | AXL_FINAL, 0, {0}},
};
#define AXL_N ((int)(sizeof AXL / sizeof AXL[0]))

/* ------------------------------------------------------------ card state --- */
#define AXL_MAXCARDS 104
#define AXL_BOXES 6
static const uint8_t AXL_GAP[AXL_BOXES] = {2, 4, 7, 13, 24, 40};   /* prompts until a card in box b comes back */
enum { AXS_NEW, AXS_PRESUMED, AXS_LEARNING };
typedef struct {
    uint8_t box;                 /* 0..5 */
    uint8_t state;               /* AXS_*: not met yet, from an earlier lesson (not taught here), being learned */
    uint8_t lapses;              /* misses */
    uint8_t acc;                 /* right answers, a running average, 0..100 */
    uint8_t asked;               /* times asked (to 255) */
    uint8_t ds;                  /* its answer time, a running average, in 100 ms */
    uint16_t due;                /* the prompt it is due on */
} ax_cs_t;

typedef struct { uint8_t card, other, n; } ax_conf_t;   /* a card answered as another (name prompts) */
#define AXL_NCONF 8

typedef struct {
    uint8_t lesson;              /* 0..AXL_N-1 */
    uint8_t ncards;              /* the cards of every lesson (ax_learn_init) */
    uint16_t t;                  /* prompts asked this session */
    uint8_t cur;                 /* the card asked (index) */
    uint8_t teach;               /* .. it is being taught, not asked */
    uint8_t last;                /* the card asked before (not twice in a row when there is a choice) */
    uint8_t streak;              /* right answers in a row */
    uint8_t since_old;           /* prompts since the last review of an earlier lesson's card */
    uint8_t right, total;        /* this lesson */
    ax_conf_t conf[AXL_NCONF];
    uint16_t card[AXL_MAXCARDS];
    uint8_t first[AXL_MAXCARDS];  /* the lesson that teaches it */
    ax_cs_t s[AXL_MAXCARDS];
} ax_learn_t;

static void ax_learn_init(ax_learn_t *L)
{
    int l, i, k;
    L->ncards = 0;
    for (l = 0; l < AXL_N; l++)
        for (i = 0; i < AXL[l].n; i++) {
            for (k = 0; k < L->ncards && L->card[k] != AXL[l].card[i]; k++)
                ;
            if (k == L->ncards && L->ncards < AXL_MAXCARDS) {
                L->card[L->ncards] = AXL[l].card[i];
                L->first[L->ncards] = (uint8_t)l;
                L->ncards++;
            }
        }
    for (i = 0; i < L->ncards; i++) {
        L->s[i].box = 0;
        L->s[i].state = AXS_NEW;
        L->s[i].lapses = L->s[i].asked = 0;
        L->s[i].acc = 0;
        L->s[i].ds = 0;
        L->s[i].due = 0;
    }
    for (i = 0; i < AXL_NCONF; i++)
        L->conf[i].n = 0;
    L->t = 0;
    L->lesson = 0;
    L->streak = L->right = L->total = 0;
    L->last = 0xFF;
}
static int ax_learn_in_pool(const ax_learn_t *L, int i) { return L->first[i] <= L->lesson; }
static int ax_learn_is_new(const ax_learn_t *L, int i) { return L->first[i] == L->lesson; }

/* a lesson begins: its own cards wait to be taught; the cards before it not met yet are presumed known, spread out
 * (a card already learned this session keeps its state) */
static void ax_learn_start(ax_learn_t *L, int lesson, uint32_t *rng)
{
    int i;
    L->lesson = (uint8_t)(lesson < 0 ? 0 : lesson >= AXL_N ? AXL_N - 1 : lesson);
    L->right = L->total = 0;
    L->streak = 0;
    L->last = 0xFF;
    L->since_old = 0;
    for (i = 0; i < L->ncards; i++) {
        ax_cs_t *c = &L->s[i];
        if (L->first[i] == L->lesson && c->state == AXS_PRESUMED) {   /* (a lesson's own cards are taught, even ones
                                                  * a later lesson presumed known) */
            c->state = AXS_NEW;
            c->box = 0;
        }
        if (L->first[i] == L->lesson && c->state == AXS_LEARNING && c->box > 2) {
            c->box = 2;                          /* (a lesson chosen again: its cards shown once more) */
            c->due = L->t;
        }
        if (L->first[i] < L->lesson && c->state == AXS_NEW) {
            c->state = AXS_PRESUMED;
            c->box = 2;
            c->acc = 80;
            c->due = (uint16_t)(L->t + 3 + ax_randn(rng, 24));
        } else if (L->first[i] < L->lesson && c->state != AXS_NEW && (int16_t)(c->due - L->t) > 30)
            c->due = (uint16_t)(L->t + 3 + ax_randn(rng, 24));   /* (an old card: back in this lesson's reach) */
    }
}

/* the next card: one due (the most overdue, weak ones first), else a new one to teach (two new cards in the works at
 * most), else the one due soonest. Returns its index; *teach set when it is to be taught */
static int ax_learn_next(ax_learn_t *L, int *teach, uint32_t *rng)
{
    int i, best = -1, young = 0, newi = -1, relearn = -1, bown = -1, bold = -1;
    int32_t bs = -0x7FFFFFFF, rs = -0x7FFFFFFF, so = -0x7FFFFFFF, sd = -0x7FFFFFFF;
    for (i = 0; i < L->ncards; i++) {
        const ax_cs_t *c = &L->s[i];
        if (!ax_learn_in_pool(L, i))
            continue;
        if (c->state == AXS_NEW) {
            if (newi < 0 && ax_learn_is_new(L, i))
                newi = i;
            continue;
        }
        if (ax_learn_is_new(L, i) && c->box < 2)
            young++;
        if ((int16_t)(L->t - c->due) >= 0 && c->state == AXS_LEARNING && c->box == 0 && i != L->last) {
            int32_t sc = (int16_t)(L->t - c->due);   /* (just missed or just taught: before anything else) */
            if (sc > rs) {
                rs = sc;
                relearn = i;
            }
        }
        if ((int16_t)(L->t - c->due) >= 0) {
            int32_t sc = (int32_t)(int16_t)(L->t - c->due) * 4 + c->lapses * 6 + (100 - c->acc) / 8 +
                         (ax_learn_is_new(L, i) ? 6 : 0) + ax_randn(rng, 3);
            if (i == L->last)
                sc -= 1000;
            if (sc > bs) {
                bs = sc;
                best = i;
            }
            if (sc > -500 && ax_learn_is_new(L, i) && sc > so) {
                so = sc;
                bown = i;
            }
            if (sc > -500 && !ax_learn_is_new(L, i) && sc > sd) {
                sd = sc;
                bold = i;
            }
        }
    }
    /* a lesson's prompts are its own cards two times in three: an earlier lesson's card once the last two were not */
    if (relearn < 0 && bold >= 0 && (L->since_old >= 2 || (bown < 0 && (newi < 0 || young >= 2)))) {
        *teach = 0;
        return bold;
    }
    if (relearn < 0 && bown >= 0) {
        *teach = 0;
        return bown;
    }
    if (relearn >= 0) {
        *teach = 0;
        return relearn;
    }
    if (newi >= 0 && young < 2) {                /* (a new card whenever fewer than two are still young) */
        *teach = 1;
        return newi;
    }
    if (best < 0 || bs < -500) {                 /* nothing due: the one due soonest (ahead of time) */
        int32_t soon = 0x7FFFFFFF;
        for (i = 0; i < L->ncards; i++) {
            const ax_cs_t *c = &L->s[i];
            int32_t d;
            if (!ax_learn_in_pool(L, i) || c->state == AXS_NEW || i == L->last)
                continue;
            d = (int16_t)(c->due - L->t);
            if (d < soon) {
                soon = d;
                best = i;
            }
        }
        if (best < 0 && newi >= 0) {
            *teach = 1;
            return newi;
        }
        if (best < 0)
            best = L->last != 0xFF ? L->last : 0;
    }
    *teach = 0;
    return best;
}

/* a card taught (shown and played once): it joins the cards being learned, asked again soon */
static void ax_learn_taught(ax_learn_t *L, int i)
{
    ax_cs_t *c = &L->s[i];
    c->state = AXS_LEARNING;
    c->box = 0;
    c->acc = 50;
    c->due = (uint16_t)(L->t + 1);
}

/* a card answered: right / wrong, how long it took (ms), the hint used; other: the card named instead (name prompts:
 * another card's index, or 0xFF) */
static void ax_learn_answer(ax_learn_t *L, int i, int right, uint32_t ms, int hinted, int other)
{
    ax_cs_t *c = &L->s[i];
    uint32_t ds = ms / 100u;
    if (ds > 255u)
        ds = 255u;
    c->ds = (uint8_t)(c->asked ? (c->ds * 3u + ds) / 4u : ds);
    if (c->asked < 255u)
        c->asked++;
    c->acc = (uint8_t)((c->acc * 3u + (right ? 100u : 0u)) / 4u);
    L->total++;
    if (right) {
        L->right++;
        if (L->streak < 255u)
            L->streak++;
        if (hinted)
            c->box = c->box > 1 ? 1 : c->box;
        else if (ms > 9000u && c->box >= 2)
            ;                                    /* (right, but slow: it stays where it is) */
        else
            c->box = (uint8_t)(c->box + (c->box == 0 && ms < 4000u ? 2 : 1));
        if (c->box >= AXL_BOXES)
            c->box = AXL_BOXES - 1;
        if (c->state == AXS_PRESUMED && !hinted) {
            c->box = c->box < 3 ? 3 : c->box;    /* (known after all) */
            c->state = AXS_LEARNING;
        }
        c->due = (uint16_t)(L->t + 1 + AXL_GAP[c->box]);
    } else {
        L->streak = 0;
        if (c->lapses < 255u)
            c->lapses++;
        c->box = 0;
        c->state = AXS_LEARNING;
        c->due = (uint16_t)(L->t + 2);
        if (other >= 0 && other != i) {          /* the tutor remembers what it was taken for */
            int k, w = 0;
            for (k = 0; k < AXL_NCONF; k++)
                if (L->conf[k].n && L->conf[k].card == i && L->conf[k].other == other)
                    break;
            if (k == AXL_NCONF) {
                for (k = 1; k < AXL_NCONF; k++)
                    if (L->conf[k].n < L->conf[w].n)
                        w = k;
                k = w;
                L->conf[k].card = (uint8_t)i;
                L->conf[k].other = (uint8_t)other;
                L->conf[k].n = 0;
            }
            if (L->conf[k].n < 255u)
                L->conf[k].n++;
        }
    }
    L->last = (uint8_t)i;
    L->t++;
    L->since_old = (uint8_t)(ax_learn_is_new(L, i) ? (L->since_old < 255u ? L->since_old + 1u : 255u) : 0u);
}

/* ------------------------------------------------------------- the tutor --- */
/* how much a card needs work: misses weigh most, then accuracy, then slowness; 0 for a card not asked */
static int ax_learn_need(const ax_learn_t *L, int i)
{
    const ax_cs_t *c = &L->s[i];
    if (!c->asked || c->state == AXS_NEW)
        return 0;
    return c->lapses * 30 + (100 - c->acc) + (c->ds > 60 ? (c->ds - 60) / 2 : 0) - c->box * 8;
}
/* the n cards that need work most (need over a threshold), into out; returns how many */
static int ax_learn_weakest(const ax_learn_t *L, int *out, int n)
{
    int got = 0, i, k;
    for (i = 0; i < L->ncards; i++) {
        int need = ax_learn_need(L, i);
        if (need < 20 || !ax_learn_in_pool(L, i))
            continue;
        for (k = got; k > 0 && ax_learn_need(L, out[k - 1]) < need; k--)
            if (k < n)
                out[k] = out[k - 1];
        if (k < n) {
            out[k] = i;
            if (got < n)
                got++;
        }
    }
    return got;
}
/* the pair most mixed up (card, other), or 0 */
static int ax_learn_confusion(const ax_learn_t *L, int *card, int *other)
{
    int k, best = -1;
    for (k = 0; k < AXL_NCONF; k++)
        if (L->conf[k].n >= 2 && (best < 0 || L->conf[k].n > L->conf[best].n))
            best = k;
    if (best < 0)
        return 0;
    *card = L->conf[best].card;
    *other = L->conf[best].other;
    return 1;
}
/* the lesson's own cards all learned: box 3 or more, 75 % or better. The final lesson: 40 prompts at 85 % */
static int ax_learn_mastered(const ax_learn_t *L)
{
    int i, any = 0;
    if (AXL[L->lesson].flags & AXL_FINAL)
        return L->total >= 40 && L->right * 100 >= L->total * 85;
    for (i = 0; i < L->ncards; i++) {
        if (!ax_learn_is_new(L, i))
            continue;
        any = 1;
        if (L->s[i].state != AXS_LEARNING || L->s[i].box < 3 || L->s[i].acc < 75)
            return 0;
    }
    return any;
}
/* how far the lesson's own cards are: 0..n boxes summed against n x 3 (for the HUD) */
static int ax_learn_progress(const ax_learn_t *L, int *of)
{
    int i, got = 0, n = 0;
    if (AXL[L->lesson].flags & AXL_FINAL) {
        *of = 40;
        return L->total > 40 ? 40 : L->total;
    }
    for (i = 0; i < L->ncards; i++)
        if (ax_learn_is_new(L, i)) {
            n += 3;
            got += L->s[i].state == AXS_NEW ? 0 : L->s[i].box > 3 ? 3 : L->s[i].box;
        }
    *of = n;
    return got;
}
/* the earliest lesson the weak cards come from (to suggest going back), or -1 */
static int ax_learn_suggest_back(const ax_learn_t *L)
{
    int w[3], n = ax_learn_weakest(L, w, 3), k, lo = -1;
    for (k = 0; k < n; k++)
        if (L->s[w[k]].lapses >= 2 && L->first[w[k]] < L->lesson && (lo < 0 || L->first[w[k]] < lo))
            lo = L->first[w[k]];
    return lo;
}

/* -------------------------------------------------------------- names --- */
static const char *const AXL_CHORD_EAR[AX_CHORDS_N] = {
    "bright and settled", "dark, sad", "small and tense", "stretched, floating", "open, no third",
    "open, wants to fall to the 3rd", "sweet, settled", "bittersweet", "dreamy", "bluesy, wants to resolve",
    "mellow", "dark and unstable", "tense, symmetric", "haunted, film noir", "stretched and bright", "suspended, soul",
    "bright and wide", "lush", "funky", "smooth", "dark tension", "the Hendrix chord", "floating, Lydian",
    "open minor", "big band"};
static const char *const AXL_FIELD_EAR[AX_FIELDS_N] = {
    "plain major", "minor with a bright 6th", "minor with a dark b2", "major with a raised #4",
    "major with a flat 7th", "plain minor", "dark, with a flat 5th", "minor, raised 7th: exotic",
    "minor going up, 6 and 7 raised", "five notes, no half steps", "five notes, the blues box",
    "the minor pentatonic + b5", "all whole steps: dreamy", "half, whole, half, whole", "whole, half, whole, half",
    "two augmented triads", "every note"};
static const char *const AXL_IVL_TIP[13] = {
    "the same note", "1 half step: the closest", "2 half steps = 1 whole", "3 half steps", "4 half steps",
    "5 half steps", "6 half steps: the tritone", "7 half steps", "8 half steps", "9 half steps", "10 half steps",
    "11 half steps", "12 half steps: an octave"};

static void ax_learn_cat(char *b, const char *s, uint32_t n)
{
    uint32_t k = 0;
    while (b[k] && k + 1 < n)
        k++;
    while (*s && k + 1 < n)
        b[k++] = *s++;
    b[k] = 0;
}
/* a chord's degrees from its mask: "1 3 5 b7" (b9 / #9 / #11 / b5 / #5 by its altered tones) */
static void ax_learn_degrees(char *b, uint32_t n, int ci)
{
    static const char *const SIMPLE[12] = {"1", "b2", "2", "b3", "3", "4", "b5", "5", "#5", "6", "b7", "7"};
    static const char *const EXT[12] = {"1", "b9", "9", "b3", "3", "11", "#11", "5", "#5", "13", "b7", "7"};
    const ax_obj_t *o = &AX_CHORDS[ci];
    int pc, ext = o->n >= 4 && ((o->mask >> 10) & 1u || (o->mask >> 11) & 1u);
    b[0] = 0;
    for (pc = 0; pc < 12; pc++) {
        const char *d;
        if (!((o->mask >> pc) & 1u))
            continue;
        d = ext ? EXT[pc] : SIMPLE[pc];
        if (pc == 3 && ((o->alt >> 3) & 1u))
            d = "#9";
        if (pc == 6 && !((o->mask >> 7) & 1u))
            d = "b5";
        if (pc == 9 && o->n == 4 && !((o->mask >> 10) & 1u))
            d = "6";
        if (b[0])
            ax_learn_cat(b, " ", n);
        ax_learn_cat(b, d, n);
    }
}
/* a card's name as a learner reads it: "E", "major 3rd", "maj7 chord", "Dorian" */
static void ax_learn_card_name(char *b, uint32_t n, uint16_t card)
{
    int k = AXC_KIND(card), i = AXC_IDX(card);
    b[0] = 0;
    switch (k) {
    case CK_NOTE: ax_learn_cat(b, AX_PART[i % 12].sym, n); break;
    case CK_IVL:
    case CK_IVL_EAR: ax_learn_cat(b, AX_IVL_NAME[i % 13], n); break;
    case CK_CHORD:
    case CK_CHORD_EAR: ax_learn_cat(b, AX_CHORDS[i % AX_CHORDS_N].name, n); break;
    case CK_MORPH: ax_learn_cat(b, "-> ", n); ax_learn_cat(b, AX_FIELDS[i % AX_FIELDS_N].name, n); break;
    default: ax_learn_cat(b, AX_FIELDS[i % AX_FIELDS_N].name, n); break;
    }
    if (k == CK_IVL_EAR || k == CK_CHORD_EAR || k == CK_FIELD_EAR)
        ax_learn_cat(b, " (ear)", n);
}
/* a card's name, short (the tutor's confusions): "M2", "maj7", "Dorian", "E" */
static void ax_learn_card_short(char *b, uint32_t n, uint16_t card)
{
    int k = AXC_KIND(card), i = AXC_IDX(card);
    b[0] = 0;
    if (k == CK_NOTE)
        ax_learn_cat(b, AX_PART[i % 12].sym, n);
    else if (k == CK_IVL || k == CK_IVL_EAR)
        ax_learn_cat(b, AX_IVL_SHORT[i % 13], n);
    else if (k == CK_CHORD || k == CK_CHORD_EAR)
        ax_learn_cat(b, AX_CHORDS[i % AX_CHORDS_N].sym[0] ? AX_CHORDS[i % AX_CHORDS_N].sym : "major", n);
    else
        ax_learn_cat(b, AX_FIELDS[i % AX_FIELDS_N].name, n);
}
/* the tip the tutor gives for a card: what it is, how to find it, what it sounds like */
static void ax_learn_tip(char *b, uint32_t n, uint16_t card)
{
    int k = AXC_KIND(card), i = AXC_IDX(card);
    b[0] = 0;
    switch (k) {
    case CK_NOTE:
        if ((0xAB5u >> i) & 1u)
            ax_learn_cat(b, i == 0 ? "C: left of the two black keys" : i == 5 ? "F: left of the three black keys" :
                         "white keys: C D E F G A B", n);
        else
            ax_learn_cat(b, "a black key: # one up, b one down", n);
        break;
    case CK_IVL: ax_learn_cat(b, AXL_IVL_TIP[i % 13], n); break;
    case CK_IVL_EAR: ax_learn_cat(b, "listen: ", n); ax_learn_cat(b, AXL_IVL_TIP[i % 13], n); break;
    case CK_CHORD: ax_learn_degrees(b, n, i); break;
    case CK_CHORD_EAR: ax_learn_cat(b, AXL_CHORD_EAR[i % AX_CHORDS_N], n); break;
    case CK_FIELD:
    case CK_MISSING: ax_learn_cat(b, AX_FIELDS[i % AX_FIELDS_N].formula, n); break;
    case CK_FIELD_EAR: ax_learn_cat(b, AXL_FIELD_EAR[i % AX_FIELDS_N], n); break;
    case CK_MORPH: ax_learn_cat(b, "change only: ", n); {
        int pc;
        for (pc = 0; pc < 12; pc++)
            if (((AX_FIELDS[i].mask >> pc) & 1u) != ((AX_FIELDS[0].mask >> pc) & 1u) && ((AX_FIELDS[i].mask >> pc) & 1u)) {
                static const char *const D[12] = {"1", "b2", "2", "b3", "3", "4", "#4", "5", "b6", "6", "b7", "7"};
                ax_learn_cat(b, D[pc], n);
                ax_learn_cat(b, " ", n);
            }
        }
        break;
    default: break;
    }
}

/* ------------------------------------------------------------- prompts --- */
/* the prompt that asks card i (its root new each time); the names offered for an ear card: the cards of its kind
 * learned so far (at most 12) */
static void ax_learn_prompt(const ax_learn_t *L, int i, ax_prompt_t *p, uint32_t *s)
{
    static const uint8_t EASY[5] = {0, 5, 7, 2, 9};     /* C F G D A */
    uint16_t card = L->card[i];
    int k = AXC_KIND(card), x = AXC_IDX(card), j, flags = AXL[L->lesson].flags;
    int root = (flags & AXL_EASYROOT) ? EASY[ax_randn(s, 5)] : ax_randn(s, 12);
    p->kind = AXP_NOTE;
    p->root = (uint8_t)root;
    p->idx = (uint8_t)x;
    p->from = p->start_key = p->missing = 0;
    p->ivl = 0;
    p->target = p->shown = p->neutral = 0;
    p->name_kind = 0xFF;
    p->name_ans = 0;
    p->nopt = 0;
    switch (k) {
    case CK_NOTE:
        p->root = (uint8_t)x;
        p->target = p->shown = (uint16_t)(1u << x);
        break;
    case CK_IVL:
    case CK_IVL_EAR: {
        int up = !((flags & AXL_DESC) && ax_randn(s, 3) == 0), start;
        start = up ? ax_randn(s, AX_NKEYS - x) : x + ax_randn(s, AX_NKEYS - x);
        if (flags & AXL_EASYROOT) {               /* (from a white key) */
            int tries = 0;
            while (!((0xAB5u >> AX_KEY_PC(start)) & 1u) && tries++ < 12)
                start = up ? ax_randn(s, AX_NKEYS - x) : x + ax_randn(s, AX_NKEYS - x);
        }
        p->kind = k == CK_IVL ? AXP_BOND_PLAY : AXP_BOND_NAME;
        p->ivl = (int8_t)(up ? x : -x);
        p->start_key = (uint8_t)start;
        p->root = AX_KEY_PC(start);
        p->target = (uint16_t)(1u << AX_KEY_PC(start + p->ivl));
        p->shown = (uint16_t)((1u << p->root) | p->target);
        if (k == CK_IVL_EAR) {
            p->name_kind = AX_BOND;
            p->name_ans = (uint8_t)x;
        }
        break;
    }
    case CK_CHORD:
    case CK_CHORD_EAR:
        p->kind = k == CK_CHORD ? AXP_CHORD_PLAY : AXP_CHORD_NAME;
        p->target = p->shown = ax_rot(AX_CHORDS[x].mask, root);
        if (k == CK_CHORD_EAR) {
            p->name_kind = AX_BOUND;
            p->name_ans = (uint8_t)x;
        }
        break;
    case CK_FIELD:
        p->kind = AXP_FIELD_PLAY;
        p->target = p->shown = ax_rot(AX_FIELDS[x].mask, root);
        break;
    case CK_FIELD_EAR:
        p->kind = AXP_FIELD_NAME;
        p->target = p->shown = ax_rot(AX_FIELDS[x].mask, root);
        p->name_kind = AX_FIELD;
        p->name_ans = (uint8_t)x;
        break;
    case CK_MISSING: {
        int m = 1 + ax_randn(s, AX_FIELDS[x].n - 1), pc, c = 0;
        for (pc = 0; pc < 12; pc++)
            if (((AX_FIELDS[x].mask >> pc) & 1u) && c++ == m)
                break;
        p->kind = AXP_FIELD_MISSING;
        p->missing = (uint8_t)((pc + root) % 12);
        p->target = (uint16_t)(1u << p->missing);
        p->shown = (uint16_t)(ax_rot(AX_FIELDS[x].mask, root) & ~p->target);
        break;
    }
    case CK_MORPH: {
        uint16_t from = ax_rot(AX_FIELDS[0].mask, root), to = ax_rot(AX_FIELDS[x].mask, root);
        p->kind = AXP_FIELD_MORPH;
        p->from = 0;
        p->target = (uint16_t)(to & ~from);
        p->neutral = (uint16_t)(to & from);
        p->shown = from;
        break;
    }
    default: break;
    }
    if (p->name_kind != 0xFF) {                  /* the names offered: this kind's cards learned so far */
        int want = k;
        for (j = 0; j < L->ncards && p->nopt < 12; j++)
            if (AXC_KIND(L->card[j]) == want && ax_learn_in_pool(L, j) && L->s[j].state != AXS_NEW)
                p->opt[p->nopt++] = (uint8_t)AXC_IDX(L->card[j]);
        for (j = 0; j < p->nopt && p->opt[j] != p->name_ans; j++)
            ;
        if (j == p->nopt && p->nopt < 12)
            p->opt[p->nopt++] = p->name_ans;     /* (the answer is always there) */
        if (p->nopt < 2) {                       /* (one name is no question: its usual neighbour too) */
            p->opt[p->nopt++] = (uint8_t)(p->name_kind == AX_BOND ? (x % 12) + 1 : p->name_kind == AX_BOUND ?
                                          (x == 0 ? 1 : 0) : (x == 0 ? 5 : 0));
        }
        for (j = 1; j < p->nopt; j++) {          /* (in catalog order: easier to scan) */
            uint8_t v = p->opt[j];
            int q = j;
            while (q > 0 && p->opt[q - 1] > v) {
                p->opt[q] = p->opt[q - 1];
                q--;
            }
            p->opt[q] = v;
        }
    }
}
/* the card index whose name option v stands for (an ear card of the same kind), or -1 */
static int ax_learn_card_of_opt(const ax_learn_t *L, int kind, int v)
{
    int j;
    for (j = 0; j < L->ncards; j++)
        if (AXC_KIND(L->card[j]) == kind && AXC_IDX(L->card[j]) == v)
            return j;
    return -1;
}
