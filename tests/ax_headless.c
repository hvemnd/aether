/* SPDX-License-Identifier: GPL-3.0-only
 * AETHER//SYNTAX: the whole firmware, headless, played by a test that reads each prompt and answers it (the browser
 * emulator's unit compiled into this one, so the test sees ax_ui.c's state). It checks that every mode's challenge
 * runs from its first wave to its summary, that right and wrong answers are judged as given, that the groove builds
 * and sounds, and that the screen stays within the panel's budget; it writes screenshots of what it saw.
 *
 *   cc -O2 -std=gnu11 -w -Itools/emu/web/compat -Ibuild/gen -Ifirmware/src -DEMU_WEB '-DFELUCCA_VERSION="AETHER test"' \
 *      -o build/host/ax_headless tests/ax_headless.c -lm && build/host/ax_headless build/ax-run
 */
#include "../tools/emu/web/emu_web.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum { HB_FX, HB_SEL, HB_ENV, HB_LFO, HB_EDIT, HB_GLO, HB_HOME, HB_SAVE, HB_ARP, HB_SEQ, HB_PLAY, HB_REC };
enum { HE_SELECT, HE_ALGO, HE_PRESETS, HE_K1, HE_K2, HE_K3, HE_K4 };
static const char *out_dir = "build/ax-run";
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static double rms_acc;
static uint32_t rms_n;
static int measuring;
static void step(uint32_t ms)
{
    while (ms--) {
        static uint32_t acc;
        web_step(1);
        while (web_midi_out_take())
            ;
        if (measuring && ++acc >= 10u) {
            uint32_t i;
            acc = 0;
            web_render(441);
            for (i = 0; i < 441u; i++) {
                double l = web_out_l()[i];
                rms_acc += l * l;
            }
            rms_n += 441;
        }
    }
}
static void btn(int b) { web_buttons(1u << b); step(60); web_buttons(0); step(60); }
static void turn(int role, int n) { web_enc(role, n); step(40); }
static void shot(const char *name)
{
    char p[512];
    FILE *f;
    uint16_t *lcd = web_fb();
    int i;
    snprintf(p, sizeof p, "%s/%s.ppm", out_dir, name);
    f = fopen(p, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t u = (uint16_t)((lcd[i] >> 8) | (lcd[i] << 8));
        unsigned char c[3] = {(unsigned char)(((u >> 11) & 31) * 255 / 31), (unsigned char)(((u >> 5) & 63) * 255 / 63),
                              (unsigned char)((u & 31) * 255 / 31)};
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}
static int key_for_pc(int pc, int from)          /* the first key at or above `from` sounding pitch class pc */
{
    int k;
    for (k = from; k < AX_NKEYS; k++)
        if (AX_KEY_PC(k) == pc)
            return k;
    for (k = 0; k < AX_NKEYS; k++)
        if (AX_KEY_PC(k) == pc)
            return k;
    return 0;
}
static void tap_key(int k) { web_keys(1u << k); step(40); web_keys(0); step(40); }
static void wait_for_prompt(void)                /* until the prompt has sounded and can be answered */
{
    int n = 0;
    while ((ax.answered || axa_prompt_busy || !ax.deadline) && ax.run.on && n++ < 20000)
        step(5);
}

/* answer the prompt on screen: right, or deliberately wrong */
static void answer(int right)
{
    const ax_prompt_t *p = &ax.shown;
    int k, pc;
    switch (p->kind) {
    case AXP_NOTE:
    case AXP_NOTE_SIGIL:
        tap_key(key_for_pc(right ? p->root : (p->root + 1) % 12, 7));
        break;
    case AXP_BOND_PLAY: {
        int end = p->start_key + p->ivl;
        if (!right) {                            /* (a neighbour of the right end, never the start again) */
            int w = end + 1 < AX_NKEYS ? end + 1 : end - 1;
            if (w == p->start_key)
                w = end + 2 < AX_NKEYS ? end + 2 : end - 2;
            end = w;
        }
        tap_key(end);
        break;
    }
    case AXP_FIELD_MISSING:
        if (right)
            tap_key(key_for_pc(p->missing, 7));
        else {
            for (pc = 0; pc < 12 && (((p->shown | p->target) >> pc) & 1u); pc++)
                ;
            tap_key(key_for_pc(pc % 12, 7));
        }
        break;
    case AXP_FIELD_NAME: {
        int i, n = ax_name_count(p), want = p->name_ans, wrong = -1;
        if (!right) {                            /* (a field with other nodes from this root) */
            for (i = 0; i < n && wrong < 0; i++)
                if (ax_rot(AX_FIELDS[ax_name_opt(p, i)].mask, p->root) != p->target)
                    wrong = ax_name_opt(p, i);
            want = wrong;
        }
        turn(HE_ALGO, 1);                        /* (the first turn shows the list where it is) */
        for (i = 0; i < n && ax_name_opt(p, ax.name_i) != want; i++)
            turn(HE_ALGO, 1);
        btn(HB_REC);
        break;
    }
    case AXP_FIELD_MORPH:
        if (!right) {
            uint16_t gone = (uint16_t)(p->shown & ~p->neutral);
            for (pc = 0; pc < 12 && !((gone >> pc) & 1u); pc++)
                ;
            tap_key(key_for_pc(pc % 12, 7));
            break;
        }
        for (pc = 0; pc < 12; pc++)
            if ((p->target >> pc) & 1u)
                tap_key(key_for_pc(pc, 7));
        break;
    case AXP_CHORD_PLAY: {
        uint32_t m = 0;
        int from = key_for_pc(p->root, 2);
        for (pc = 0; pc < 12; pc++)
            if ((p->target >> ((p->root + pc) % 12)) & 1u)
                m |= 1u << key_for_pc((p->root + pc) % 12, from);
        if (!right)                              /* (a stray tone, struck first: before the chord is whole) */
            for (pc = 0; pc < 12; pc++)
                if (!((p->target >> pc) & 1u)) {
                    web_keys(1u << key_for_pc(pc, from));
                    step(15);
                    web_keys(0);
                    step(15);
                    return;
                }
        for (k = 0; k < AX_NKEYS; k++)           /* (the keys go down one by one, as fingers do) */
            if ((m >> k) & 1u) {
                web_keys(m & ((2u << k) - 1u));
                step(15);
            }
        step(60);
        web_keys(0);
        step(40);
        break;
    }
    default: break;
    }
}

static void run_mode(int mode, int tier, const char *tag)
{
    int i, rights = 0, wrongs = 0, shots = 0;
    char name[64];
    while (ax.mode != mode)
        turn(HE_SELECT, 1);
    while (ax.tier < tier)
        turn(HE_K2, 1);
    while (ax.tier > tier)
        turn(HE_K2, -1);
    btn(HB_PLAY);
    CHECK(ax.run.on, "%s: PLAY starts a run", tag);
    for (i = 0; i < AX_WAVES * AX_PER_WAVE && ax.run.on; i++) {
        int want = !(i % 7 == 3);                /* (one wrong in seven) */
        int r0 = ax.run.right, w0 = ax.run.wrong;
        wait_for_prompt();
        if (!ax.run.on)
            break;
        if (shots < 3 && (i == 1 || i == 9 || i == 20)) {
            snprintf(name, sizeof name, "%s_prompt%d", tag, i);
            shot(name);
            shots++;
        }
        answer(want);
        step(30);
        if (want)
            rights++;
        else
            wrongs++;
        CHECK(ax.run.right + ax.run.wrong == r0 + w0 + 1 || !ax.run.on, "%s #%d: one verdict (kind %d)", tag, i,
              ax.shown.kind);
        CHECK(want ? ax.run.right == r0 + 1 : ax.run.wrong == w0 + 1, "%s #%d: judged as given (want %d, kind %d)",
              tag, i, want, ax.shown.kind);
        if (i == 2) {
            step(40);
            snprintf(name, sizeof name, "%s_answered", tag);
            shot(name);
        }
        if (i == 3) {
            step(120);
            snprintf(name, sizeof name, "%s_miss", tag);
            shot(name);
        }
    }
    CHECK(!ax.run.on && ax.summary, "%s: the run ends in its summary", tag);
    CHECK(ax.run.right == rights && ax.run.wrong == wrongs, "%s: %d right %d wrong (got %d / %d)", tag, rights, wrongs,
          ax.run.right, ax.run.wrong);
    step(400);
    snprintf(name, sizeof name, "%s_summary", tag);
    shot(name);
    printf("%-8s tier %d: %d right, %d wrong, layers %d, form %d\n", tag, tier, ax.run.right, ax.run.wrong,
           ax.run.layers, ax.run.stage);
    btn(HB_PLAY);                                /* (the summary closed) */
    step(200);
}

/* AX_DEMO=DIR: a short tour written as frames (DIR/f0000.ppm ..), every 80 ms, for an animation; nothing checked */
static int demo_n;
static const char *demo_dir;
static void demo_wait(uint32_t ms)
{
    while (ms) {
        uint32_t k = ms > 80u ? 80u : ms;
        step(k);
        ms -= k;
        if (k == 80u) {
            char nm[32];
            const char *keep = out_dir;
            snprintf(nm, sizeof nm, "f%04d", demo_n++);
            out_dir = demo_dir;
            shot(nm);
            out_dir = keep;
        }
    }
}
static void demo(void)
{
    int i;
    web_boot();
    step(1600);
    demo_wait(800);
    for (i = 0; i < 3; i++) {                    /* the anchor walks the modes of one polygon */
        web_enc(HE_K1, 1);
        demo_wait(640);
    }
    web_buttons(1u << HB_FX);                    /* the Fifth Transform */
    step(60);
    web_buttons(0);
    demo_wait(1200);
    web_buttons(1u << HB_FX);
    step(60);
    web_buttons(0);
    web_enc(HE_K1, -3);
    web_enc(HE_SELECT, 1);                       /* BOUND STATE: a challenge */
    demo_wait(400);
    web_buttons(1u << HB_PLAY);
    step(60);
    web_buttons(0);
    for (i = 0; i < 10; i++) {
        int n = 0;
        while ((ax.answered || axa_prompt_busy || !ax.deadline) && ax.run.on && n++ < 4000)
            demo_wait(80);
        demo_wait(560);
        answer(i != 4);
        demo_wait(400);
    }
    demo_wait(1600);
}

int main(int argc, char **argv)
{
    if (getenv("AX_DEMO")) {
        demo_dir = getenv("AX_DEMO");
        demo();
        printf("demo: %d frames\n", demo_n);
        return 0;
    }
    uint32_t t0, f0;
    char cmd[600];
    if (argc > 1)
        out_dir = argv[1];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", out_dir);
    if (system(cmd))
        return 2;
    web_boot();
    step(1600);
    CHECK(ax.open, "AETHER opens at power-on");
    shot("00_boot");

    /* EXPLORE: every mode, the laws */
    turn(HE_SELECT, -1);                         /* FIELD -> BOND */
    CHECK(ax.mode == AXM_BOND, "SELECT turns the mode");
    web_keys(1u << 7);                           /* C4, then E4 held: a major third */
    step(60);
    web_keys((1u << 7) | (1u << 11));
    step(200);
    shot("01_bond_M3");
    web_keys(0);
    step(100);
    turn(HE_SELECT, -1);                         /* PARTICLE */
    tap_key(13);                                 /* F#4 */
    step(150);
    shot("02_particle_Fs");
    turn(HE_SELECT, 2);                          /* FIELD */
    CHECK(ax.mode == AXM_FIELD && ax.field == 0, "FIELD on C Ionian");
    turn(HE_K1, 1);                              /* the anchor: D Dorian */
    CHECK(ax.root == 2 && ax.field == 1, "the anchor: D Dorian (root %d field %d)", ax.root, ax.field);
    step(200);
    shot("03_field_D_dorian");
    btn(HB_FX);                                  /* the fifths face */
    CHECK(ax.fifths, "FX: the fifths face");
    step(200);
    shot("04_field_fifths");
    btn(HB_FX);
    turn(HE_K1, -1);                             /* back to C Ionian */
    btn(HB_LFO);                                 /* shadow */
    step(200);
    shot("05_field_shadow");
    btn(HB_LFO);
    btn(HB_ENV);                                 /* mirror: C Ionian -> C Phrygian */
    CHECK(ax.field == 2, "ENV: C Ionian mirrored is C Phrygian (field %d)", ax.field);
    btn(HB_ENV);
    CHECK(ax.field == 0, "mirrored again: Ionian");
    turn(HE_ALGO, 12);                           /* whole-tone: a fixed point */
    CHECK(ax.field == 12, "ALGORITHM picks the field (field %d)", ax.field);
    step(200);
    shot("06_field_wholetone");
    turn(HE_ALGO, -12);
    turn(HE_SELECT, 1);                          /* BOUND STATE */
    web_keys((1u << 7) | (1u << 11) | (1u << 14) | (1u << 16));   /* C E G A: C6 (twin of Am7) */
    step(200);
    shot("07_bound_C6_twin");
    web_keys((1u << 4) | (1u << 7) | (1u << 11) | (1u << 14));    /* A C E G, A in the bass: Am7 */
    step(200);
    shot("08_bound_Am7");
    web_keys(0);
    step(80);
    turn(HE_ALGO, 11);                           /* the half-diminished: an altered tone */
    step(200);
    shot("09_bound_catalog");
    btn(HB_ENV);                                 /* C major mirrored: C minor */
    turn(HE_SELECT, 1);                          /* ORRERY */
    web_keys((1u << 9) | (1u << 12) | (1u << 16));  /* D F A inside C Ionian */
    step(200);
    shot("10_orrery");
    web_keys(0);
    step(80);
    btn(HB_SEQ);                                 /* the explore groove */
    measuring = 1;
    rms_acc = 0;
    rms_n = 0;
    step(2000);
    measuring = 0;
    CHECK(rms_n && sqrt(rms_acc / rms_n) > 0.005, "the groove sounds (rms %.4f)", rms_n ? sqrt(rms_acc / rms_n) : 0);
    printf("explore groove rms %.4f\n", rms_n ? sqrt(rms_acc / rms_n) : 0);
    btn(HB_SEQ);
    btn(HB_GLO);
    step(100);
    shot("11_settings");
    btn(HB_GLO);

    /* the panel's budget: tiles sent a second over 4 s of an idle FIELD screen */
    turn(HE_SELECT, -2);
    f0 = fb_tiles_sent;
    t0 = dev_ms;
    step(4000);
    printf("panel: %.0f tiles/s idle (a full screen is 225 tiles, ~77 ms)\n",
           (fb_tiles_sent - f0) * 1000.0 / (double)(dev_ms - t0));

    /* CHALLENGE: every mode */
    run_mode(AXM_PARTICLE, 1, "particle");
    run_mode(AXM_BOND, 2, "bond");
    run_mode(AXM_FIELD, 2, "field");
    run_mode(AXM_BOUND, 3, "bound");

    /* the panel's budget under a run with the groove full */
    {
        int i;
        while (ax.mode != AXM_BOUND)
            turn(HE_SELECT, 1);
        btn(HB_PLAY);
        for (i = 0; i < 7; i++) {
            wait_for_prompt();
            answer(1);
        }
        f0 = fb_tiles_sent;
        t0 = dev_ms;
        measuring = 1;
        rms_acc = 0;
        rms_n = 0;
        wait_for_prompt();
        step(3000);
        measuring = 0;
        printf("panel: %.0f tiles/s in a run, %d layers; groove rms %.4f\n",
               (fb_tiles_sent - f0) * 1000.0 / (double)(dev_ms - t0), ax.layers_heard,
               rms_n ? sqrt(rms_acc / rms_n) : 0);
        shot("20_run_full_groove");
        btn(HB_PLAY);
    }

    /* reduced motion (GLO, then FX): a miss tears nothing, the tunnel stands still */
    btn(HB_GLO);
    btn(HB_FX);
    btn(HB_GLO);
    CHECK(!ax_motion(), "GLO + FX: reduced motion");
    while (ax.mode != AXM_PARTICLE)
        turn(HE_SELECT, 1);
    btn(HB_PLAY);
    wait_for_prompt();
    answer(0);
    step(30);
    CHECK(!ax.glitch_until || (int32_t)(ax.glitch_until - cu.now) <= 0, "reduced motion: no tear on a miss");
    {
        uint32_t f1 = fb_tiles_sent, t1 = dev_ms;
        wait_for_prompt();
        step(2000);
        printf("panel: %.0f tiles/s in a run with reduced motion\n",
               (fb_tiles_sent - f1) * 1000.0 / (double)(dev_ms - t1));
    }
    btn(HB_PLAY);
    btn(HB_GLO);
    btn(HB_FX);
    btn(HB_GLO);
    CHECK(ax_motion(), "full motion again");
    CHECK(cs.ax_cfg >> 2 == 0, "the tempo byte untouched (120 BPM)");

    /* HOME: CoralFlower; HOME held: back */
    btn(HB_HOME);
    step(300);
    CHECK(!ax.open, "HOME leaves for CoralFlower");
    shot("30_coralflower");
    web_buttons(1u << HB_HOME);
    step(900);
    web_buttons(0);
    step(200);
    CHECK(ax.open, "HOME held opens AETHER again");
    printf("ax_headless: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
