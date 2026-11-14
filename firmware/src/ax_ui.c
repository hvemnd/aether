/* SPDX-License-Identifier: GPL-3.0-only
 * AETHER//SYNTAX: the screen, the input and the lights of the Lattice (docs/AETHER.md). Included at the end of
 * cf_screens.c: the framebuffer (cf_fb.c), the fonts, the UI's state (cf_state.h) and the queue to the audio side
 * (cf_out.c, ax_audio.c) are all there. cf_ui.c and cf_screens.c hand AETHER the keys, buttons, encoders, lights and
 * the frame while it is open (ax_open()).
 *
 * The look is Rez-like: wireframe vectors on black, everything on a sixteenth-note grid, the world building up with
 * every right answer. The panel takes ~77 ms for a whole screen (cf_fb.c), so motion is stepped on the grid and kept
 * to lines: what does not change costs nothing.
 *
 * Controls (the FM-1's own; docs/AETHER.md):
 *   SELECT  mode (PARTICLE, BOND, FIELD, BOUND STATE, ORRERY)   PRESETS  the root (Law of Rotation)
 *   ALGORITHM  the field or chord shown; in a challenge, the name to answer with
 *   K1 anchor (Law of the Anchor)  K2 tier  K3 tempo  K4 groove level
 *   PLAY start / stop a challenge  REC answer with the name chosen  SEL hint  ARP hear the prompt again
 *   FX fifths face  ENV mirror (Law of Reflection)  LFO shadow (Law of the Void)  SEQ the groove in EXPLORE
 *   GLO settings (reduced motion, flash limit)  OCT- / OCT+ octave  HOME back to CoralFlower (hold HOME there: back here)
 */
#include "ax_core.c"

/* ---------------------------------------------------------------- layout --- */
#define AXC_X 120                                /* the Lattice's centre and radius */
#define AXC_Y 100
#define AXC_R 58
#define AX_Y0 14                                 /* the stage */
#define AX_Y1 186
#define AX_BAND 190                              /* the prompt band: never glitched, never covered */
#define AX_HOME_HOLD_MS 700u

/* ---------------------------------------------------------------- colours --- */
enum { AXK_BG, AXK_S1, AXK_S2, AXK_S3, AXK_CYAN, AXK_WHITE, AXK_GREY, AXK_DIM, AXK_RED, AXK_TEAL, AXK_MAG, AXK_AMBER,
       AXK_AMBER_D, AXK_S0, AXK_GOLD = 52, AXK_GOLD_D };
#define AXK_HUE(pc, lv) ((uint8_t)(16 + ((((pc) % 12) + 12) % 12) * 3 + (lv)))   /* lv 0 bright, 1 mid, 2 dim */

static uint32_t ax_hsv(int h, int s, int v)      /* h 0..359, s v 0..255 */
{
    int r, g, b, f = (h % 60) * 255 / 60, p = v * (255 - s) / 255, q = v * (255 - s * f / 255) / 255,
        t = v * (255 - s * (255 - f) / 255) / 255;
    switch ((h / 60) % 6) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
    }
    return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}
static void ax_palette(void)
{
    static const uint32_t BASE[16] = {0x06040C, 0x0F1630, 0x1C2E54, 0x31609A, 0x5CE4FF, 0xF2F6FF, 0x8A9ABE, 0x4A5676,
                                      0xFF2A58, 0x22FFE2, 0xFF3ACB, 0xFFB43A, 0x5E3E12, 0x0A0D1C, 0, 0};
    static const uint8_t V[3] = {255, 150, 66};
    uint32_t pal[64];
    int i, pc, lv;
    for (i = 0; i < 64; i++)
        pal[i] = i < 16 ? BASE[i] : 0;
    for (pc = 0; pc < 12; pc++)                  /* C red, a twelfth of the colour wheel a semitone */
        for (lv = 0; lv < 3; lv++)
            pal[16 + pc * 3 + lv] = ax_hsv(pc * 30, lv == 0 ? 200 : 230, V[lv]);
    pal[AXK_GOLD] = 0xFFE066;
    pal[AXK_GOLD_D] = 0x3A3010;
#if !defined CF_EMU && !defined CF_PANEL_PLAIN
    for (i = 0; i < 64; i++)
        pal[i] = cf_panel_fix(pal[i], i >= 16 || i == AXK_CYAN || i == AXK_RED || i == AXK_TEAL || i == AXK_MAG);
#endif
    fb_palette(pal, 64);
}

/* ------------------------------------------------------------------- state --- */
typedef struct {
    int16_t x, y, vx, vy;                        /* 1/16 px; 1/16 px per 16 ms */
    uint8_t life, c, kind;                       /* life: 0 dead, counts down in 8 ms steps */
} axp_t;
enum { AXPK_BURST, AXPK_ASH, AXPK_DUST };
#define AX_NPART 96
typedef struct {
    uint32_t due;                                /* the sixteenth it fires on */
    uint8_t what;                                /* AXQ_* */
    uint8_t pc, note, n;
    int16_t x, y;
} ax_q_t;
enum { AXQ_BURST, AXQ_NOTE_ON, AXQ_NOTE_OFF, AXQ_HIT };
#define AX_NQ 24

static struct {
    uint8_t open;
    uint8_t mode;
    uint8_t root;                                /* the Lattice's root (PRESETS) */
    int8_t field;                                /* FIELD / ORRERY: the field shown (-1: no name for it) */
    uint16_t fmask;                              /* .. its nodes (absolute) */
    uint8_t chord;                               /* BOUND STATE with no keys: the chord shown */
    uint8_t fifths, shadow, tier, groove, level, settings;
    int8_t oct;
    uint32_t held;                               /* the keys down */
    int8_t key_last, key_prev;                   /* the last two keys struck (BOND) */
    uint32_t key_ms[AX_NKEYS];                   /* when each key was struck (its node flashes) */
    /* a challenge */
    ax_run_t run;
    ax_prompt_t shown;                           /* the prompt on screen: the run's, or the one just answered (the run
                                                  * has already made the next) until the next is asked */
    uint16_t locked;                             /* the nodes found (lock-on) */
    uint8_t hinted, answered, verdict;           /* this prompt: hint used; answered (waiting for the next); AXJ_* */
    int16_t name_i;                              /* the name ALGORITHM is on */
    uint8_t name_on;                             /* .. it has been turned this prompt */
    uint32_t p_step;                             /* the sixteenth the prompt was asked on */
    uint32_t deadline;                           /* .. it times out on (0: waiting for its sound to end) */
    uint32_t next_step;                          /* answered: the next prompt comes on this sixteenth */
    uint32_t reveal_until;                       /* a miss: the answer glows until this sixteenth */
    uint32_t glitch_until, wash_until, wash_t0;  /* (ms) */
    uint8_t wash_pc;
    uint8_t summary;                             /* a run ended: its stats shown */
    uint32_t summary_until;                      /* .. until this sixteenth */
    uint8_t strip;                               /* .. the groove strips back a layer a bar */
    uint32_t strip_step;
    uint8_t layers_heard;                        /* the layers the groove was last told */
    char say[40];                                /* a line for a moment (over the band's second line) */
    uint32_t say_until;
    uint32_t home_down;                          /* (CoralFlower) HOME went down: held, it opens AETHER */
    uint32_t flash_ms[4];                        /* the last full-screen flashes (the flash limit) */
    axp_t p[AX_NPART];
    ax_q_t q[AX_NQ];
    uint32_t rng;
    uint32_t last_beat;                          /* the last downbeat's sixteenth (the frame's pulse) */
    uint32_t beat_ms;
} ax;

static int ax_open(void) { return ax.open; }
static int ax_motion(void) { return !(cs.ax_cfg & 1u); }          /* reduced motion off */
static int ax_flash_limit(void) { return !(cs.ax_cfg & 2u); }
static int ax_bpm(void) { uint32_t t = cs.ax_cfg >> 2; return t ? 78 + 2 * (int)t : 120; }
static void ax_set_bpm(int b)
{
    b = clampi(b, 80, 160) & ~1;
    cs.ax_cfg = (uint8_t)((cs.ax_cfg & 3u) | (uint8_t)(((b - 78) / 2) << 2));
    cf_post(CFE_AX, AXA_TEMPO, 0, b);
}
static uint32_t ax_now_step(void) { return axa_steps; }
static void ax_cat(char *d, const char *s, uint32_t max)   /* (the unit's libc has str_cpy, not a strcat) */
{
    uint32_t n = str_len(d);
    if (n < max)
        str_cpy(d + n, s, max - n);
}
static uint32_t ax_step_ms(void) { return 60000u / (uint32_t)(ax_bpm() * 4); }

static void ax_say(const char *s, uint32_t ms)
{
    str_cpy(ax.say, s, sizeof ax.say);
    ax.say_until = cu.now + ms;
}

/* ------------------------------------------------------------- the queue --- */
static void ax_q(uint32_t due, int what, int pc, int note, int n, int x, int y)
{
    int i;
    for (i = 0; i < AX_NQ; i++)
        if (ax.q[i].what == 0xFF)
            break;
    if (i == AX_NQ)
        return;
    ax.q[i].due = due;
    ax.q[i].what = (uint8_t)what;
    ax.q[i].pc = (uint8_t)pc;
    ax.q[i].note = (uint8_t)note;
    ax.q[i].n = (uint8_t)n;
    ax.q[i].x = (int16_t)x;
    ax.q[i].y = (int16_t)y;
}
static void ax_q_clear(void)
{
    int i;
    for (i = 0; i < AX_NQ; i++)
        ax.q[i].what = 0xFF;
}

/* ------------------------------------------------------------------- math --- */
static int ax_sin(int a) { return AX_SIN[a & 255]; }
static int ax_cos(int a) { return AX_SIN[(a + 64) & 255]; }
static int ax_isqrt(int v)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= v)
        r++;
    return r;
}
/* node pc's position on a ring: place 0 at the top, clockwise */
static int ax_node_angle(int pc, int fifths) { return (ax_face_place(pc, fifths) * 256 + 6) / 12 - 64; }
static void ax_node_xy(int cx, int cy, int r, int pc, int fifths, int *x, int *y)
{
    int a = ax_node_angle(pc, fifths);
    *x = cx + ((r * ax_cos(a)) >> 14);
    *y = cy + ((r * ax_sin(a)) >> 14);
}

/* ------------------------------------------------------------- primitives --- */
static void ax_disc(int cx, int cy, int r, uint8_t c)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int w = ax_isqrt(r * r - dy * dy);
        fb_hline(cx - w, cy + dy, 2 * w + 1, c);
    }
}
static void ax_circle(int cx, int cy, int r, uint8_t c)
{
    int x = r, y = 0, e = 1 - r;
    while (x >= y) {
        fb_pset(cx + x, cy + y, c); fb_pset(cx - x, cy + y, c); fb_pset(cx + x, cy - y, c); fb_pset(cx - x, cy - y, c);
        fb_pset(cx + y, cy + x, c); fb_pset(cx - y, cy + x, c); fb_pset(cx + y, cy - x, c); fb_pset(cx - y, cy - x, c);
        y++;
        if (e < 0)
            e += 2 * y + 1;
        else {
            x--;
            e += 2 * (y - x) + 1;
        }
    }
}
/* an arc from the top, clockwise, frac/256 of a turn */
static void ax_arc(int cx, int cy, int r, int frac, uint8_t c)
{
    int a, px = cx, py = cy - r;
    for (a = 1; a <= frac; a++) {
        int x = cx + ((r * ax_cos(a - 64)) >> 14), y = cy + ((r * ax_sin(a - 64)) >> 14);
        fb_line(px, py, x, y, c);
        px = x;
        py = y;
    }
}
static void ax_line2(int x0, int y0, int x1, int y1, uint8_t c)   /* two pixels wide */
{
    int dx = x1 - x0, dy = y1 - y0;
    fb_line(x0, y0, x1, y1, c);
    if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy))
        fb_line(x0, y0 + 1, x1, y1 + 1, c);
    else
        fb_line(x0 + 1, y0, x1 + 1, y1, c);
}
/* a convex polygon (points in order round the ring) filled with a diagonal hatch */
static void ax_hatch(const int *xs, const int *ys, int n, uint8_t c, int dens)
{
    int y, i, ymin = 999, ymax = -999;
    if (n < 3)
        return;
    for (i = 0; i < n; i++) {
        ymin = ys[i] < ymin ? ys[i] : ymin;
        ymax = ys[i] > ymax ? ys[i] : ymax;
    }
    for (y = ymin; y <= ymax; y++) {
        int xl = 999, xr = -999, x;
        for (i = 0; i < n; i++) {
            int j = (i + 1) % n, y0 = ys[i], y1 = ys[j], x0 = xs[i], x1 = xs[j];
            if ((y >= y0 && y <= y1) || (y >= y1 && y <= y0)) {
                int xi = y1 == y0 ? x0 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
                if (y1 == y0) {
                    xl = x0 < xl ? x0 : xl;
                    xr = x0 > xr ? x0 : xr;
                    xi = x1;
                }
                xl = xi < xl ? xi : xl;
                xr = xi > xr ? xi : xr;
            }
        }
        for (x = xl; x <= xr; x++)
            if (((x + y) & (dens - 1)) == 0)
                fb_pset(x, y, c);
    }
}

/* -------------------------------------------------------------- the sigil --- */
typedef struct {
    int cx, cy, r;
    uint16_t mask;               /* the lit nodes (absolute) */
    uint16_t alt;                /* .. notched (absolute) */
    uint16_t ghost;              /* hollow nodes: a question's dark node */
    uint16_t glow;               /* pulsing rings: an answer shown, a hint */
    uint16_t locked;             /* reticles: lock-on */
    uint16_t struck;             /* just played: a white flash */
    int root;                    /* -1: none */
    uint8_t kind;                /* AX_* */
    uint8_t fifths, shimmer, quiet;
    int8_t dir;                  /* a bond: +1 up, -1 down (an arrowhead), 0 none */
    int8_t bond_to;              /* a bond: its other end */
} ax_sig_t;

static void ax_sigil(const ax_sig_t *s)
{
    int xs[12], ys[12], pcs[12], n = 0, i, pl, x, y, rc = s->root >= 0 ? s->root : 0;
    uint8_t line = s->quiet ? AXK_HUE(rc, 2) : AXK_HUE(rc, 1);
    uint32_t pulse = (cu.now / 120u) & 1u;
    ax_circle(s->cx, s->cy, s->r, AXK_S1);
    for (i = 0; i < 12; i++) {                   /* the twelve nodes, always: the Divine All behind everything */
        ax_node_xy(s->cx, s->cy, s->r, i, s->fifths, &x, &y);
        fb_rect(x - 1, y - 1, 2, 2, AXK_S2);
    }
    for (pl = 0; pl < 12; pl++)                  /* the lit nodes in ring order (on this face) */
        for (i = 0; i < 12; i++)
            if (ax_face_place(i, s->fifths) == pl && ((s->mask >> i) & 1u)) {
                ax_node_xy(s->cx, s->cy, s->r, i, s->fifths, &xs[n], &ys[n]);
                pcs[n++] = i;
            }
    switch (s->kind) {
    case AX_PARTICLE:
        for (i = 0; i < n; i++) {
            ax_line2(s->cx, s->cy, xs[i], ys[i], AXK_HUE(pcs[i], 1));
            ax_circle(xs[i], ys[i], 12, AXK_HUE(pcs[i], 2));
            ax_circle(xs[i], ys[i], 18, AXK_HUE(pcs[i], 2));
        }
        break;
    case AX_BOND:
        if (n == 2 || s->bond_to >= 0) {
            int ax0, ay0, ax1, ay1, mx, my, dx, dy, len, cls, k, to = s->bond_to >= 0 ? s->bond_to : pcs[1];
            ax_node_xy(s->cx, s->cy, s->r, rc, s->fifths, &ax0, &ay0);
            ax_node_xy(s->cx, s->cy, s->r, to, s->fifths, &ax1, &ay1);
            ax_line2(ax0, ay0, ax1, ay1, line);
            mx = (ax0 + ax1) / 2;
            my = (ay0 + ay1) / 2;
            dx = ax1 - ax0;
            dy = ay1 - ay0;
            len = ax_isqrt(dx * dx + dy * dy);
            cls = ax_bond_class(to - rc);
            for (k = 0; k < cls && len; k++) {   /* tick marks across the line: the bond's class */
                int o = (k - (cls - 1) / 2) * 5 - ((cls - 1) & 1) * 2;
                int tx = mx + dx * o / len, ty = my + dy * o / len;
                fb_line(tx - dy * 5 / len, ty + dx * 5 / len, tx + dy * 5 / len, ty - dx * 5 / len, AXK_WHITE);
            }
            if (s->dir && len) {                 /* the arrowhead at the bond's far end */
                int bx = ax1 - dx * 10 / len, by = ay1 - dy * 10 / len;
                fb_line(ax1, ay1, bx - dy * 5 / len, by + dx * 5 / len, AXK_WHITE);
                fb_line(ax1, ay1, bx + dy * 5 / len, by - dx * 5 / len, AXK_WHITE);
            }
        }
        break;
    case AX_BOUND:
        ax_hatch(xs, ys, n, AXK_HUE(rc, s->quiet ? 2 : 1), s->quiet ? 4 : 2);
        for (i = 0; i < n && n > 1; i++)
            ax_line2(xs[i], ys[i], xs[(i + 1) % n], ys[(i + 1) % n], s->quiet ? AXK_HUE(rc, 2) : AXK_HUE(rc, 0));
        break;
    default:                                     /* a field: an outline, spokes in */
        for (i = 0; i < n && n > 1; i++)
            fb_line(xs[i], ys[i], xs[(i + 1) % n], ys[(i + 1) % n], line);
        for (i = 0; i < n; i++)
            fb_line(xs[i], ys[i], xs[i] + (s->cx - xs[i]) / 7, ys[i] + (s->cy - ys[i]) / 7, AXK_HUE(pcs[i], 2));
        break;
    }
    for (i = 0; i < n; i++) {                    /* the nodes */
        int pc = pcs[i], hot = (s->struck >> pc) & 1u, rr = s->quiet ? 3 : 4;
        ax_disc(xs[i], ys[i], rr + hot, AXK_HUE(pc, s->quiet ? 1 : 0));
        if (hot)                                 /* (played: a white ring, the colour kept) */
            ax_circle(xs[i], ys[i], rr + 3, AXK_WHITE);
        if (s->shimmer && pulse)
            ax_circle(xs[i], ys[i], rr + 2, AXK_WHITE);
        if ((s->alt >> pc) & 1u) {               /* an altered tone: notched */
            fb_rect(xs[i] - 1, ys[i] - rr, 2, rr, AXK_BG);
        }
    }
    for (i = 0; i < 12; i++) {
        if (!(((s->ghost | s->glow | s->locked) >> i) & 1u))
            continue;
        ax_node_xy(s->cx, s->cy, s->r, i, s->fifths, &x, &y);
        if ((s->ghost >> i) & 1u) {
            ax_circle(x, y, 5, AXK_GREY);
            fb_text_c(&CF_FONT_MICRO, x, y - 2, "?", AXK_WHITE, 1);
        }
        if ((s->glow >> i) & 1u) {
            ax_circle(x, y, 7 + (int)pulse, AXK_HUE(i, 0));
            ax_circle(x, y, 10 + (int)pulse, AXK_HUE(i, 1));
        }
        if ((s->locked >> i) & 1u) {             /* a reticle that turns */
            int a = (int)(cu.now / 8u) & 255, k;
            for (k = 0; k < 4; k++) {
                int aa = a + k * 64, c0 = ax_cos(aa), s0 = ax_sin(aa);
                fb_line(x + ((8 * c0) >> 14), y + ((8 * s0) >> 14), x + ((13 * c0) >> 14), y + ((13 * s0) >> 14),
                        AXK_CYAN);
            }
            ax_circle(x, y, 9, AXK_CYAN);
        }
    }
    if (s->root >= 0 && ((s->mask | s->ghost) >> s->root & 1u)) {   /* the root's double ring */
        ax_node_xy(s->cx, s->cy, s->r, s->root, s->fifths, &x, &y);
        ax_circle(x, y, s->quiet ? 6 : 7, AXK_HUE(s->root, 0));
        if (!s->quiet)
            ax_circle(x, y, 10, AXK_HUE(s->root, 1));
    }
}

/* the charge: six bars, the bonds of class 1..6 inside a set */
static void ax_charge(int x, int y, uint16_t mask)
{
    int v[6] = {0}, i, j, k;
    for (i = 0; i < 12; i++)
        for (j = i + 1; j < 12; j++)
            if (((mask >> i) & (mask >> j) & 1u))
                v[ax_bond_class(j - i) - 1]++;
    for (k = 0; k < 6; k++) {
        int h = v[k] * 2 > 14 ? 14 : v[k] * 2;
        fb_rect(x + k * 5, y - h, 3, h, AXK_S3);
        if (h)
            fb_rect(x + k * 5, y - h, 3, 1, AXK_CYAN);
        fb_rect(x + k * 5, y + 1, 3, 1, AXK_S2);
    }
}

/* ---------------------------------------------------------------- the tunnel --- */
/* the field's polygon repeated into the distance, flying forward a step every sixteenth */
static void ax_tunnel(uint16_t mask, int fifths, uint32_t step, int split)
{
    int vx[12], pl[12], n = 0, i, ring, k;
    uint32_t ph = ax_motion() ? (step / 4u) % 4u : 0u;    /* (a step on every beat: the panel's budget, and the pulse) */
    int rot = ax_motion() ? (int)((step / 16u) * 2u % 256u) : 0;
    if (ax_popc(mask) < 3)
        mask = AX_ALL;
    for (k = 0; k < 12; k++)
        for (i = 0; i < 12; i++)
            if (ax_face_place(i, fifths) == k && ((mask >> i) & 1u)) {
                vx[n] = ax_node_angle(i, fifths) + rot;
                pl[n++] = i;
            }
    (void)pl;
    for (ring = 0; ring < 7; ring++) {
        int z = 20 + ring * 8 - (int)ph * 2, r = 4400 / (z > 1 ? z : 1), c;
        int px = 0, py = 0, fx = 0, fy = 0;
        if (r < AXC_R + 14 || r > 230)
            continue;
        c = r > 150 ? AXK_S3 : r > 100 ? AXK_S2 : AXK_S1;
        for (i = 0; i <= n; i++) {
            int a = vx[i % n], x = AXC_X + ((r * ax_cos(a)) >> 14), y = AXC_Y + ((r * ax_sin(a)) >> 14);
            if (i) {
                if (split) {
                    fb_line(px - 2, py, x - 2, y, AXK_RED);
                    fb_line(px + 2, py, x + 2, y, AXK_TEAL);
                } else
                    fb_line(px, py, x, y, (uint8_t)c);
            } else {
                fx = x;
                fy = y;
            }
            px = x;
            py = y;
        }
        (void)fx;
        (void)fy;
    }
    for (i = 0; i < n; i++) {                    /* the rails */
        int a = vx[i];
        fb_line(AXC_X + (((AXC_R + 14) * ax_cos(a)) >> 14), AXC_Y + (((AXC_R + 14) * ax_sin(a)) >> 14),
                AXC_X + ((240 * ax_cos(a)) >> 14), AXC_Y + ((240 * ax_sin(a)) >> 14), AXK_S1);
    }
}

/* -------------------------------------------------------------- particles --- */
static void ax_spawn(int x, int y, int n, int pc, int kind)
{
    int i, k = 0;
    for (i = 0; i < AX_NPART && k < n; i++) {
        axp_t *p = &ax.p[i];
        int a, sp;
        if (p->life)
            continue;
        a = (int)(ax_rand(&ax.rng) & 255u);
        sp = kind == AXPK_ASH ? 4 + (int)(ax_rand(&ax.rng) % 8u) : kind == AXPK_DUST ? 2 + (int)(ax_rand(&ax.rng) % 4u) :
             14 + (int)(ax_rand(&ax.rng) % 26u);
        p->x = (int16_t)(x * 16);
        p->y = (int16_t)(y * 16);
        p->vx = (int16_t)((sp * ax_cos(a)) >> 14);
        p->vy = (int16_t)(kind == AXPK_ASH ? 4 + (int)(ax_rand(&ax.rng) % 10u) : (sp * ax_sin(a)) >> 14);
        p->life = (uint8_t)(kind == AXPK_DUST ? 255 : 60 + (int)(ax_rand(&ax.rng) % 50u));
        p->c = (uint8_t)pc;
        p->kind = (uint8_t)kind;
        k++;
    }
}
static void ax_particles_step(uint32_t dt, int beat)   /* beat: a beat began (the dust drifts on it) */
{
    int i, steps = (int)(dt / 8u);
    if (steps < 1)
        steps = 1;
    if (steps > 12)
        steps = 12;
    for (i = 0; i < AX_NPART; i++) {
        axp_t *p = &ax.p[i];
        int k;
        if (!p->life)
            continue;
        if (p->kind == AXPK_DUST) {              /* (the dust moves on the beat only: a still pixel costs the panel
                                                  * nothing, a moving one two tiles a frame) */
            if (beat) {
                p->x = (int16_t)(p->x + p->vx * 4);
                p->y = (int16_t)(p->y + p->vy * 4);
                if (p->life > 24)
                    p->life = (uint8_t)(p->life - 24);
                else
                    p->life = 0;
            }
            if (p->y < AX_Y0 * 16 || p->y >= AX_Y1 * 16 || p->x < 0 || p->x >= FB_W * 16)
                p->life = 0;
            continue;
        }
        for (k = 0; k < steps && p->life; k++) {
            p->x = (int16_t)(p->x + p->vx / 2);
            p->y = (int16_t)(p->y + p->vy / 2);
            if (p->kind == AXPK_BURST) {
                p->vx = (int16_t)(p->vx * 15 / 16);
                p->vy = (int16_t)(p->vy * 15 / 16 + 1);
            }
            p->life--;
        }
        if (p->y < AX_Y0 * 16 || p->y >= AX_Y1 * 16 || p->x < 0 || p->x >= FB_W * 16)
            p->life = 0;
    }
}
static void ax_particles_draw(void)
{
    int i;
    for (i = 0; i < AX_NPART; i++) {
        const axp_t *p = &ax.p[i];
        int x = p->x / 16, y = p->y / 16;
        uint8_t c;
        if (!p->life)
            continue;
        if (p->kind == AXPK_ASH)
            c = p->life > 40 ? AXK_RED : AXK_MAG;
        else if (p->kind == AXPK_DUST)
            c = AXK_S3;
        else
            c = p->life > 70 ? AXK_WHITE : p->life > 40 ? AXK_HUE(p->c, 0) : AXK_HUE(p->c, 1);
        if (p->kind == AXPK_BURST && p->life > 50)
            fb_rect(x, y, 2, 2, c);
        else
            fb_pset(x, y, c);
    }
}

/* ------------------------------------------------------------- Ascension --- */
/* the player's form: point, line, triangle, tetrahedron, octahedron, icosahedron (12 vertices, one per note) */
static const int16_t AX_V_TRI[3][3] = {{0, -256, 0}, {222, 128, 0}, {-222, 128, 0}};
static const int16_t AX_V_TET[4][3] = {{148, 148, 148}, {148, -148, -148}, {-148, 148, -148}, {-148, -148, 148}};
static const int16_t AX_V_OCT[6][3] = {{256, 0, 0}, {-256, 0, 0}, {0, 256, 0}, {0, -256, 0}, {0, 0, 256}, {0, 0, -256}};
static const int16_t AX_V_ICO[12][3] = {{0, 135, 218}, {0, 135, -218}, {0, -135, 218}, {0, -135, -218},
                                        {135, 218, 0}, {135, -218, 0}, {-135, 218, 0}, {-135, -218, 0},
                                        {218, 0, 135}, {-218, 0, 135}, {218, 0, -135}, {-218, 0, -135}};
static void ax_form(int cx, int cy, int sc, int stage, uint32_t t)
{
    const int16_t (*v)[3];
    int n, i, j, px[12], py[12], ay = (int)(t / 24u) & 255, axr = (int)(t / 61u) & 255, edge2;
    switch (stage) {
    case 0: ax_disc(cx, cy, 2, AXK_CYAN); ax_circle(cx, cy, 5 + (int)((t / 300u) & 1u), AXK_S3); return;
    case 1: {
        int c0 = ax_cos(ay), dx = (sc * c0) >> 14, dy = (sc * ax_sin(ay) / 3) >> 14;
        ax_line2(cx - dx, cy - dy, cx + dx, cy + dy, AXK_CYAN);
        ax_disc(cx - dx, cy - dy, 2, AXK_WHITE);
        ax_disc(cx + dx, cy + dy, 2, AXK_WHITE);
        return;
    }
    case 2: v = AX_V_TRI; n = 3; edge2 = 0; break;
    case 3: v = AX_V_TET; n = 4; edge2 = 0; break;
    case 4: v = AX_V_OCT; n = 6; edge2 = 2 * 256 * 256; break;
    default: v = AX_V_ICO; n = 12; edge2 = 4 * 135 * 135; break;
    }
    for (i = 0; i < n; i++) {                    /* turn about y, then x; a little perspective */
        int x = v[i][0], y = v[i][1], z = v[i][2], x1, z1, y1, z2;
        x1 = (x * ax_cos(ay) + z * ax_sin(ay)) >> 14;
        z1 = (-x * ax_sin(ay) + z * ax_cos(ay)) >> 14;
        y1 = (y * ax_cos(axr) - z1 * ax_sin(axr)) >> 14;
        z2 = (y * ax_sin(axr) + z1 * ax_cos(axr)) >> 14;
        px[i] = cx + x1 * sc * 1024 / (1024 + z2) / 256;
        py[i] = cy + y1 * sc * 1024 / (1024 + z2) / 256;
    }
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++) {
            int dx = v[i][0] - v[j][0], dy = v[i][1] - v[j][1], dz = v[i][2] - v[j][2], d = dx * dx + dy * dy + dz * dz;
            if (!edge2 || (d > edge2 * 9 / 10 && d < edge2 * 11 / 10))
                fb_line(px[i], py[i], px[j], py[j], stage == 5 ? AXK_GOLD : AXK_CYAN);
        }
    for (i = 0; i < n; i++)
        fb_rect(px[i] - 1, py[i] - 1, 2, 2, stage == 5 ? AXK_HUE(i, 0) : AXK_WHITE);
}

/* ------------------------------------------------------------- what is shown --- */
typedef struct {
    ax_sig_t s;
    char a[24];                  /* the band's big line */
    char b[40];                  /* .. its second */
    uint16_t field;              /* the tunnel's polygon (absolute) */
    int8_t chord_root;           /* ORRERY: the chord held, inside the field */
    uint16_t chord;
} ax_view_t;

static void ax_note_oct(int key, char *b, uint32_t n)
{
    int m = AX_KEY_NOTE(key) + 12 * ax.oct;
    char d[4];
    str_cpy(b, ax_note_name(m % 12), n);
    d[0] = (char)('0' + (m / 12 - 1 + 10) % 10);
    d[1] = 0;
    ax_cat(b, d, n);
}
static void ax_obj_name(char *b, uint32_t n, int root, const ax_obj_t *o, int kind)
{
    str_cpy(b, ax_note_name(root), n);
    if (kind == AX_BOUND) {
        ax_cat(b, o->sym[0] ? o->sym : " major", n);   /* (a major triad's symbol is the bare root: said in full) */
    } else {
        ax_cat(b, " ", n);
        ax_cat(b, o->name, n);
    }
}
static void ax_sig_init(ax_sig_t *s, int kind, uint16_t mask, int root)
{
    s->cx = AXC_X;
    s->cy = AXC_Y;
    s->r = AXC_R;
    s->mask = mask;
    s->alt = s->ghost = s->glow = s->locked = s->struck = 0;
    s->root = root;
    s->kind = (uint8_t)kind;
    s->fifths = ax.fifths;
    s->shimmer = s->quiet = 0;
    s->dir = 0;
    s->bond_to = -1;
}
static uint16_t ax_struck_mask(void)
{
    uint16_t m = 0;
    int k;
    for (k = 0; k < AX_NKEYS; k++)
        if (((ax.held >> k) & 1u) || (ax.key_ms[k] && cu.now - ax.key_ms[k] < 160u))
            m |= (uint16_t)(1u << AX_KEY_PC(k));
    return m;
}
static void ax_field_name(char *b, uint32_t n, int field, int root)
{
    if (field >= 0)
        ax_obj_name(b, n, root, &AX_FIELDS[field], AX_FIELD);
    else {
        str_cpy(b, ax_note_name(root), n);
        ax_cat(b, " unnamed field", n);
    }
}

static void ax_view_explore(ax_view_t *v)
{
    uint16_t held_pcs = ax_pcs_of_keys(ax.held);
    ax_match_t m[4];
    int k, low = ax_lowest_key(ax.held);
    v->field = ax.fmask;
    v->chord = 0;
    v->chord_root = -1;
    v->a[0] = v->b[0] = 0;
    switch (ax.mode) {
    case AXM_PARTICLE: {
        int key = ax.key_last >= 0 ? ax.key_last : -1, pc = key >= 0 ? AX_KEY_PC(key) : ax.root;
        ax_sig_init(&v->s, AX_PARTICLE, (uint16_t)(1u << pc), pc);
        if (key >= 0)
            ax_note_oct(key, v->a, sizeof v->a);
        else
            str_cpy(v->a, ax_note_name(pc), sizeof v->a);
        str_cpy(v->b, AX_PART[pc].cosmic, sizeof v->b);
        ax_cat(v->b, "  node ", sizeof v->b);
        {
            char d[4];
            d[0] = (char)(pc >= 10 ? '1' : '0' + pc);
            d[1] = pc >= 10 ? (char)('0' + pc - 10) : 0;
            d[2] = 0;
            ax_cat(v->b, d, sizeof v->b);
        }
        v->field = (uint16_t)(1u << pc);
        break;
    }
    case AXM_BOND: {
        int k0 = -1, k1 = -1, semis, cls;
        if (ax_popc(ax.held) >= 2) {            /* (two held: the lowest and the highest; the first struck the root) */
            for (k = 0; k < AX_NKEYS; k++)
                if ((ax.held >> k) & 1u) {
                    if (k0 < 0)
                        k0 = k;
                    k1 = k;
                }
            if (ax.key_last == k0) {
                k0 = k1;
                k1 = ax.key_last;
            }
        } else if (ax.key_prev >= 0) {
            k0 = ax.key_prev;
            k1 = ax.key_last;
        }
        if (k0 < 0) {
            ax_sig_init(&v->s, AX_BOND, (uint16_t)(1u << ax.root), ax.root);
            str_cpy(v->a, "BOND", sizeof v->a);
            str_cpy(v->b, "hold a key, strike another", sizeof v->b);
            break;
        }
        semis = k1 - k0;
        cls = ax_bond_class(semis);
        ax_sig_init(&v->s, AX_BOND, (uint16_t)((1u << AX_KEY_PC(k0)) | (1u << AX_KEY_PC(k1))), AX_KEY_PC(k0));
        v->s.bond_to = (int8_t)AX_KEY_PC(k1);
        v->s.dir = (int8_t)(semis > 0 ? 1 : semis < 0 ? -1 : 0);
        {
            int a = semis < 0 ? -semis : semis;
            char n0[8], n1[8];
            str_cpy(v->a, a <= 12 ? AX_IVL_SHORT[a] : "wide", sizeof v->a);
            ax_cat(v->a, semis > 0 ? " up" : semis < 0 ? " down" : "", sizeof v->a);
            ax_note_oct(k0, n0, sizeof n0);
            ax_note_oct(k1, n1, sizeof n1);
            str_cpy(v->b, a <= 12 ? AX_IVL_NAME[a] : "compound", sizeof v->b);
            ax_cat(v->b, "  ", sizeof v->b);
            ax_cat(v->b, cls ? AX_BONDS[cls - 1].cosmic : "unison", sizeof v->b);
        }
        v->field = v->s.mask;
        break;
    }
    case AXM_FIELD:
    case AXM_ORRERY: {
        int f = ax.field;
        ax_sig_init(&v->s, AX_FIELD, ax.fmask, ax.root);
        v->s.struck = held_pcs;
        if (f >= 0 && AX_FIELDS[f].period < 12)
            v->s.shimmer = 1;
        ax_field_name(v->a, sizeof v->a, f, ax.root);
        if (ax.mode == AXM_FIELD) {
            str_cpy(v->b, f >= 0 ? AX_FIELDS[f].cosmic : "a field the catalog has no name for", sizeof v->b);
        } else {                                 /* ORRERY: the chord held, inside the field */
            if (ax_popc(held_pcs) >= 3 && ax_recognize(held_pcs, AX_BOUND, low >= 0 ? AX_KEY_PC(low) : -1, m, 4)) {
                v->chord = held_pcs;
                v->chord_root = (int8_t)m[0].root;
                ax_obj_name(v->b, sizeof v->b, m[0].root, &AX_CHORDS[m[0].idx], AX_BOUND);
                ax_cat(v->b, (held_pcs & ~ax.fmask) ? "  outside the field" : "  inside the field", sizeof v->b);
            } else if (held_pcs) {
                v->chord = held_pcs;
                str_cpy(v->b, "play a chord inside the field", sizeof v->b);
            } else
                str_cpy(v->b, "the whole theory on one ring", sizeof v->b);
        }
        break;
    }
    case AXM_BOUND:
    default: {
        if (ax_popc(held_pcs) >= 2) {
            int got = ax_recognize(held_pcs, AX_BOUND, AX_KEY_PC(low), m, 4);
            if (got) {
                const ax_obj_t *o = &AX_CHORDS[m[0].idx];
                ax_sig_init(&v->s, AX_BOUND, held_pcs, m[0].root);
                v->s.alt = ax_rot(o->alt, m[0].root);
                v->s.shimmer = o->period < 12;
                ax_obj_name(v->a, sizeof v->a, m[0].root, o, AX_BOUND);
                if (got > 1) {                   /* a twin: the same nodes, another anchor */
                    str_cpy(v->b, "twin of ", sizeof v->b);
                    {
                        char t[12];
                        ax_obj_name(t, sizeof t, m[1].root, &AX_CHORDS[m[1].idx], AX_BOUND);
                        ax_cat(v->b, t, sizeof v->b);
                    }
                } else {
                    ax_match_t fm[1];
                    char d[8];
                    int nf = ax_containing_fields(held_pcs, fm, 1);
                    str_cpy(v->b, o->cosmic, sizeof v->b);
                    ax_cat(v->b, "  in ", sizeof v->b);
                    d[0] = (char)('0' + (nf / 10) % 10);
                    d[1] = (char)('0' + nf % 10);
                    d[2] = 0;
                    ax_cat(v->b, nf >= 10 ? d : d + 1, sizeof v->b);
                    ax_cat(v->b, " fields", sizeof v->b);
                }
            } else {
                ax_sig_init(&v->s, AX_BOUND, held_pcs, AX_KEY_PC(low));
                v->s.quiet = 1;
                str_cpy(v->a, "unnamed", sizeof v->a);
                str_cpy(v->b, "a bound state with no name", sizeof v->b);
            }
            v->s.struck = held_pcs;
        } else {
            const ax_obj_t *o = &AX_CHORDS[ax.chord % AX_CHORDS_N];
            ax_sig_init(&v->s, AX_BOUND, ax_rot(o->mask, ax.root), ax.root);
            v->s.alt = ax_rot(o->alt, ax.root);
            v->s.shimmer = o->period < 12;
            ax_obj_name(v->a, sizeof v->a, ax.root, o, AX_BOUND);
            str_cpy(v->b, o->cosmic, sizeof v->b);
            ax_cat(v->b, "  ", sizeof v->b);
            ax_cat(v->b, o->name, sizeof v->b);
        }
        v->field = v->s.mask;
        break;
    }
    }
    if (ax.shadow) {                             /* the Law of the Void: the dark nodes lit */
        uint16_t sh = (uint16_t)(~v->s.mask & AX_ALL);
        int got = ax_recognize(sh, AX_FIELD, -1, m, 1);
        if (!got)
            got = ax_recognize(sh, AX_BOUND, -1, m, 1) ? 2 : 0;
        v->s.mask = sh;
        v->s.kind = AX_FIELD;
        v->s.quiet = 1;
        v->s.root = got ? m[0].root : -1;
        v->s.alt = 0;
        str_cpy(v->b, "shadow: ", sizeof v->b);
        if (got == 1) {
            char t[24];
            ax_field_name(t, sizeof t, m[0].idx, m[0].root);
            ax_cat(v->b, t, sizeof v->b);
        } else if (got == 2) {
            char t[16];
            ax_obj_name(t, sizeof t, m[0].root, &AX_CHORDS[m[0].idx], AX_BOUND);
            ax_cat(v->b, t, sizeof v->b);
        } else
            ax_cat(v->b, "no name", sizeof v->b);
    }
}

static void ax_view_challenge(ax_view_t *v)
{
    const ax_prompt_t *p = &ax.shown;
    int reveal = ax.reveal_until && (int32_t)(ax.reveal_until - ax_now_step()) > 0;
    v->a[0] = v->b[0] = 0;
    v->chord = 0;
    v->chord_root = -1;
    switch (p->kind) {
    case AXP_NOTE:
    case AXP_NOTE_SIGIL:
        ax_sig_init(&v->s, AX_PARTICLE, p->shown, p->root);
        if (p->kind == AXP_NOTE) {
            str_cpy(v->a, "FIND ", sizeof v->a);
            ax_cat(v->a, ax_note_name(p->root), sizeof v->a);
            str_cpy(v->b, "press it, any octave", sizeof v->b);
            if (!ax.answered && !reveal) {       /* (the name, not the picture, until it is answered) */
                v->s.mask = 0;
                v->s.root = -1;
            }
        } else {
            str_cpy(v->a, "FIND THIS", sizeof v->a);
            str_cpy(v->b, "the particle on the ring", sizeof v->b);
        }
        break;
    case AXP_BOND_PLAY: {
        char n0[8];
        int a = p->ivl < 0 ? -p->ivl : p->ivl;
        ax_sig_init(&v->s, AX_BOND, (uint16_t)(1u << p->root), p->root);
        ax_note_oct(p->start_key, n0, sizeof n0);
        str_cpy(v->a, "BOND ", sizeof v->a);
        ax_cat(v->a, p->ivl > 0 ? "UP" : "DOWN", sizeof v->a);
        str_cpy(v->b, "from ", sizeof v->b);
        ax_cat(v->b, n0, sizeof v->b);
        ax_cat(v->b, ": play the other end", sizeof v->b);
        if (reveal || ax.answered) {
            v->s.mask = p->shown;
            v->s.bond_to = (int8_t)(((int)p->root + p->ivl + 120) % 12);
            v->s.dir = (int8_t)(p->ivl > 0 ? 1 : -1);
            str_cpy(v->b, AX_IVL_NAME[a], sizeof v->b);
        }
        break;
    }
    case AXP_FIELD_MISSING:
        ax_sig_init(&v->s, AX_FIELD, p->shown, p->root);
        v->s.ghost = p->target;
        str_cpy(v->a, "MISSING NODE", sizeof v->a);
        ax_field_name(v->b, sizeof v->b, p->idx, p->root);
        break;
    case AXP_FIELD_NAME:
        ax_sig_init(&v->s, AX_FIELD, ax.answered || reveal ? p->shown : 0, p->root);
        str_cpy(v->a, "NAME IT", sizeof v->a);
        str_cpy(v->b, "listen, turn ALGO, press REC", sizeof v->b);
        break;
    case AXP_FIELD_MORPH:
        ax_sig_init(&v->s, AX_FIELD, p->shown, p->root);
        str_cpy(v->a, "MORPH", sizeof v->a);
        str_cpy(v->b, AX_FIELDS[p->from].name, sizeof v->b);
        ax_cat(v->b, " -> ", sizeof v->b);
        ax_cat(v->b, AX_FIELDS[p->idx].name, sizeof v->b);
        ax_cat(v->b, " on ", sizeof v->b);
        ax_cat(v->b, ax_note_name(p->root), sizeof v->b);
        if (ax.answered && ax.verdict == AXJ_RIGHT)
            v->s.mask = (uint16_t)(p->target | p->neutral);
        break;
    case AXP_CHORD_PLAY:
    default:
        ax_sig_init(&v->s, AX_BOUND, ax.answered || reveal ? p->target : 0, p->root);
        v->s.alt = ax_rot(AX_CHORDS[p->idx].alt, p->root);
        str_cpy(v->a, "PLAY ", sizeof v->a);
        {
            char t[16];
            ax_obj_name(t, sizeof t, p->root, &AX_CHORDS[p->idx], AX_BOUND);
            ax_cat(v->a, t, sizeof v->a);
        }
        str_cpy(v->b, AX_CHORDS[p->idx].cosmic, sizeof v->b);
        ax_cat(v->b, ", any voicing", sizeof v->b);
        break;
    }
    v->s.locked = ax.locked;
    v->s.struck = ax_struck_mask();
    if (p->kind == AXP_CHORD_PLAY || p->kind == AXP_FIELD_MORPH)
        v->s.mask |= ax.locked;
    if (reveal || ax.hinted)
        v->s.glow = p->target;
    if (p->name_kind != 0xFF && ax.name_on && !ax.answered) {   /* the name ALGORITHM is on */
        int o = ax_name_opt(p, ax.name_i);
        str_cpy(v->b, "< ", sizeof v->b);
        ax_cat(v->b, p->name_kind == AX_BOND ? AX_IVL_NAME[o] : AX_FIELDS[o].name, sizeof v->b);
        ax_cat(v->b, " >  REC", sizeof v->b);
    }
    /* the tunnel never tells: the whole ring until the answer is out, a question's own visible nodes at most */
    if (ax.answered || reveal)
        v->field = p->kind == AXP_BOND_PLAY || p->kind <= AXP_NOTE_SIGIL ? AX_ALL : (uint16_t)(p->shown | p->target);
    else
        v->field = p->kind == AXP_FIELD_MISSING || p->kind == AXP_FIELD_MORPH ? p->shown : AX_ALL;
}

/* ----------------------------------------------------------- talking to sound --- */
static void ax_send_groove(int layers)
{
    ax.layers_heard = (uint8_t)layers;
    cf_post(CFE_AX, AXA_GROOVE, (uint32_t)layers, ax.level);
}
static void ax_send_field(int root, uint16_t mask)
{
    cf_post(CFE_AX, AXA_FIELD, (uint32_t)root, (int32_t)mask);
}
static void ax_explore_sound(void)
{
    if (ax.run.on || ax.summary)
        return;
    if (ax.mode == AXM_FIELD || ax.mode == AXM_ORRERY)
        ax_send_field(ax.root, ax.fmask);
    else if (ax.mode == AXM_BOUND)
        ax_send_field(ax.root, ax_rot(AX_CHORDS[ax.chord].mask, ax.root));
    else
        ax_send_field(ax.root, ax_rot(AX_FIELDS[0].mask, ax.root));
    ax_send_groove(ax.groove ? 3 : 0);
}
/* the prompt's sound: a bond (its start, then its end), a field (up from its root); others are silent */
static void ax_prompt_sound(void)
{
    const ax_prompt_t *p = &ax.shown;
    int i, o = 12 * ax.oct;
    cf_post(CFE_AX, AXA_PROMPT_CLR, 0, 0);
    if (p->kind == AXP_BOND_PLAY) {
        cf_post(CFE_AX, AXA_PROMPT_ADD, (uint32_t)(AX_KEY_NOTE(p->start_key) + o), 0);
        cf_post(CFE_AX, AXA_PROMPT_ADD, (uint32_t)(AX_KEY_NOTE(p->start_key + p->ivl) + o), 0);
        cf_post(CFE_AX, AXA_PROMPT_GO, 0, 4);
    } else if (p->kind == AXP_FIELD_NAME) {
        int base = 60 + p->root + o;
        for (i = 0; i < 12; i++)
            if ((AX_FIELDS[p->idx].mask >> i) & 1u)
                cf_post(CFE_AX, AXA_PROMPT_ADD, (uint32_t)(base + i), 0);
        cf_post(CFE_AX, AXA_PROMPT_ADD, (uint32_t)(base + 12), 0);
        cf_post(CFE_AX, AXA_PROMPT_GO, 0, 2);
    }
}

/* -------------------------------------------------------------- a challenge --- */
static int ax_timeout_steps(void)
{
    static const uint8_t T[AXM_N][3] = {{48, 32, 24}, {64, 48, 40}, {80, 64, 56}, {96, 80, 64}, {64, 64, 64}};
    return T[ax.run.mode % AXM_N][(ax.run.tier - 1) % 3];
}
static void ax_prompt_begin(void)
{
    const ax_prompt_t *p = &ax.run.p;
    ax.shown = ax.run.p;
    ax.locked = 0;
    ax.hinted = ax.answered = 0;
    ax.verdict = AXJ_NONE;
    ax.name_on = 0;
    ax.name_i = 0;
    ax.p_step = ax_now_step();
    ax.deadline = 0;                             /* (set once its sound has played: ax_frame) */
    ax.reveal_until = 0;
    /* the groove's notes: the question's visible nodes, never the answer's (a chord's arpeggio would play it) */
    if (p->kind <= AXP_NOTE_SIGIL)               /* (a note to find: the groove stays in one key, C, so it never names it) */
        ax_send_field(0, AX_FIELDS[0].mask);
    else
        ax_send_field(p->root, p->kind == AXP_FIELD_MISSING || p->kind == AXP_FIELD_MORPH ? p->shown :
                      (uint16_t)(1u << p->root));
    ax_prompt_sound();
}
static void ax_run_begin(void)
{
    if (ax.mode == AXM_ORRERY) {
        ax_say("the Orrery is for play: pick a mode", 1500);
        return;
    }
    ax_q_clear();
    ax_run_start(&ax.run, ax.mode, ax.tier, (cu.now * 2654435761u) ^ 0xAE7E5u);
    ax.summary = 0;
    ax.strip = 0;
    ax_send_groove(0);
    cf_post(CFE_AX, AXA_PULSE, 1, 0);            /* (the run's heartbeat until the first layer) */
    ax_prompt_begin();
    ax_say("WAVE 1", 1200);
}
static void ax_run_stop(void)
{
    cf_post(CFE_AX, AXA_PULSE, 0, 0);
    ax.run.on = 0;
    ax.summary = 0;
    ax.strip = 0;
    ax.locked = 0;
    cf_post(CFE_AX, AXA_PROMPT_CLR, 0, 0);
    ax_explore_sound();
}
static int ax_flash_ok(void)                     /* the flash limit: three full-screen flashes a second at most */
{
    int i, n = 0;
    if (!ax_flash_limit())
        return 1;
    for (i = 0; i < 4; i++)
        n += ax.flash_ms[i] && cu.now - ax.flash_ms[i] < 1000u;
    if (n >= 3)
        return 0;
    for (i = 0; i < 4; i++)
        if (!ax.flash_ms[i] || cu.now - ax.flash_ms[i] >= 1000u) {
            ax.flash_ms[i] = cu.now ? cu.now : 1u;
            break;
        }
    return 1;
}
static void ax_answer(int verdict)
{
    const ax_prompt_t *p = &ax.shown;
    uint32_t s = ax_now_step() + 1u, i;
    int f, x, y, k = 0, pc;
    uint16_t tgt = p->target;
    if (ax.answered)
        return;
    ax.answered = 1;
    ax.verdict = (uint8_t)verdict;
    cf_post(CFE_AX, AXA_PROMPT_CLR, 0, 0);
    if (verdict == AXJ_RIGHT) {                  /* lock-on: every node of the answer fires, one a sixteenth */
        if (p->kind == AXP_FIELD_MORPH)
            tgt = p->target;
        for (pc = 0; pc < 12; pc++) {
            int pcc = (p->root + pc) % 12, note;
            if (!((tgt >> pcc) & 1u))
                continue;
            ax_node_xy(AXC_X, AXC_Y, AXC_R, pcc, ax.fifths, &x, &y);
            note = 60 + p->root + pc + 12 * ax.oct;
            ax_q(s + (uint32_t)k, AXQ_BURST, pcc, 0, 10, x, y);
            if (p->kind == AXP_CHORD_PLAY || p->kind == AXP_FIELD_MORPH) {
                ax_q(s + (uint32_t)k, AXQ_NOTE_ON, pcc, note, 1, 0, 0);
                ax_q(s + (uint32_t)k + 2u, AXQ_NOTE_OFF, pcc, note, 1, 0, 0);
            }
            k++;
        }
        ax_q(s, AXQ_HIT, 0, 4, 1, 70, 0);       /* (the shimmer: an open hat) */
        cf_post(CFE_AX, AXA_MOTIF, (uint32_t)(60 + ((p->kind == AXP_BOND_PLAY) ? (p->root + p->ivl + 120) % 12 :
                                                   p->root)), 0);
    } else {
        ax.glitch_until = ax_motion() ? cu.now + 16u * ax_step_ms() / 2u : 0u;
        ax.reveal_until = ax_now_step() + 16u;
        cf_post(CFE_AX, AXA_HIT, 8, 80);        /* (rim, twice: the miss, no pitch in it) */
        ax_q(s, AXQ_HIT, 0, 8, 1, 60, 0);
        ax_spawn(AXC_X, AXC_Y, 18, 0, AXPK_ASH);
    }
    f = ax_run_answer(&ax.run, verdict, ax.hinted);
    ax_send_groove(ax.run.layers);
    ax.next_step = ax_now_step() + (verdict == AXJ_RIGHT ? 8u : 20u);
    if (f & AXR_WAVE_CLEAR) {
        char b[16];
        ax.wash_pc = (uint8_t)p->root;
        if (ax_flash_ok()) {
            ax.wash_until = cu.now + 4u * 4u * ax_step_ms();
            ax.wash_t0 = cu.now;
        }
        for (i = 0; i < 4; i++)                  /* the fill: toms down */
            ax_q(s + 8u + i * 2u, AXQ_HIT, 0, i < 2 ? 7 : 5, 1, 80, 0);
        if (f & AXR_RUN_END) {
            cf_post(CFE_AX, AXA_PULSE, 0, 0);
            ax.summary = 1;
            ax.summary_until = ax_now_step() + 64u;
            ax.strip = 1;
            ax.strip_step = ax_now_step() + 16u;
            ax_say("RUN COMPLETE", 2000);
        } else {
            str_cpy(b, "WAVE ", sizeof b);
            b[5] = (char)('1' + ax.run.wave);
            b[6] = 0;
            ax_say(b, 1500);
        }
    }
    if (f & AXR_STAGE_UP)
        ax_spawn(212, 38, 16, 9, AXPK_BURST);
}

/* -------------------------------------------------------------- the input --- */
static void ax_enter(void)
{
    int i;
    ax.open = 1;
    ax_q_clear();
    for (i = 0; i < AX_NPART; i++)
        ax.p[i].life = 0;
    if (!ax.tier) {                              /* (the first time since power-on) */
        ax.tier = 1;
        ax.level = 100;
        ax.mode = AXM_FIELD;
        ax.root = 0;
        ax.field = 0;
        ax.fmask = AX_FIELDS[0].mask;
        ax.key_last = ax.key_prev = -1;
        ax.rng = 0xA37E5u;
        if (cs.boot_sound && !cr_safe)
            cf_boot_go = 1;                      /* (the power-on bells, as CoralFlower's intro rings them) */
    }
    ax.held = 0;
    cu.scr = SCR_HOME;
    cu.pop = POP_NONE;
    cf_post(CFE_AX, AXA_ENTER, 0, 0);
    cf_post(CFE_AX, AXA_TEMPO, 0, ax_bpm());
    ax_palette();
    fb_full = 1;
    ax_explore_sound();
    ax_say("AETHER//SYNTAX: the Lattice opens", 2000);
}
static void ax_leave(void)
{
    ax.open = 0;
    ax.run.on = 0;
    ax.summary = 0;
    cf_post(CFE_AX, AXA_LEAVE, 0, 0);
    cf_palette_build();
    fb_full = 1;
    cf_send_config();
    ui_open(SCR_HOME);
    cu.idle_ms = 0;                              /* (straight to HOME, not the intro) */
    cf_message("Hold HOME for AETHER//SYNTAX");
}

static void ax_key(int key, int down)
{
    int note = AX_KEY_NOTE(key) + 12 * ax.oct;
    if (key < 0 || key >= AX_NKEYS)
        return;
    if (down) {
        ax.held |= 1u << key;
        ax.key_prev = ax.key_last;
        ax.key_last = (int8_t)key;
        ax.key_ms[key] = cu.now ? cu.now : 1u;
        cf_post(CFE_AX, AXA_NOTE, (uint32_t)note, cs.vel ? cs.vel : 100);
        if (!ax.run.on || ax.answered || ax.summary) {
            int x, y;
            ax_node_xy(AXC_X, AXC_Y, AXC_R, AX_KEY_PC(key), ax.fifths, &x, &y);
            ax_q(ax_now_step() + 1u, AXQ_BURST, AX_KEY_PC(key), 0, 5, x, y);
            return;
        }
        if (axa_prompt_busy)                     /* (the question still sounding: listen first) */
            return;
        {
            int v = ax_judge_key(&ax.shown, key, ax.held, &ax.locked);
            if (v != AXJ_NONE)
                ax_answer(v);
        }
    } else {
        ax.held &= ~(1u << key);
        cf_post(CFE_AX, AXA_NOTE, (uint32_t)note, 0);
        if (ax.run.on && !ax.answered && ax.shown.kind == AXP_CHORD_PLAY)
            ax.locked = ax_pcs_of_keys(ax.held) & ax.shown.target;   /* (a key let go: its reticle goes) */
    }
}

static void ax_set_field(int f)
{
    ax.field = (int8_t)f;
    ax.fmask = ax_rot(AX_FIELDS[f].mask, ax.root);
}
static void ax_enc1(int role, int d);
static void ax_enc(int role, int d)              /* (a fast turn brings several detents: each one counts) */
{
    int n = d < 0 ? -d : d;
    while (n-- > 0)
        ax_enc1(role, d > 0 ? 1 : -1);
}
static void ax_enc1(int role, int d)
{
    switch (role) {
    case EN_SELECT:
        if (ax.run.on) {
            ax_say("PLAY stops the challenge first", 1200);
            break;
        }
        ax.mode = (uint8_t)wrapi(ax.mode + (d > 0 ? 1 : -1), AXM_N);
        ax.glitch_until = ax_motion() ? cu.now + 90u : 0u;   /* (the dial's glitch between modes) */
        ax.shadow = 0;
        cf_post(CFE_AX, AXA_HIT, 9, 30);        /* (a cowbell tick: the dial's detent) */
        ax_explore_sound();
        break;
    case EN_PRESET:                              /* the root: the Law of Rotation */
        if (ax.run.on)
            break;
        ax.root = (uint8_t)wrapi(ax.root + (d > 0 ? 1 : -1), 12);
        ax.fmask = ax_rot(ax.fmask, d > 0 ? 1 : -1);
        ax_explore_sound();
        break;
    case EN_ALGO:
        if (ax.run.on && !ax.answered && ax.shown.name_kind != 0xFF) {
            if (ax.name_on)
                ax.name_i = (int16_t)(ax.name_i + (d > 0 ? 1 : -1));
            ax.name_on = 1;
            break;
        }
        if (ax.run.on)
            break;
        if (ax.mode == AXM_BOUND)
            ax.chord = (uint8_t)wrapi(ax.chord + (d > 0 ? 1 : -1), AX_CHORDS_N);
        else
            ax_set_field(wrapi((ax.field < 0 ? 0 : ax.field) + (d > 0 ? 1 : -1), AX_FIELDS_N));
        ax_explore_sound();
        break;
    case EN_K1: {                                /* the anchor: the root to the next node of the same polygon */
        int nr, f;
        if (ax.run.on || (ax.mode != AXM_FIELD && ax.mode != AXM_ORRERY))
            break;
        f = ax_anchor_step(ax.fmask, ax.root, d, &nr);
        ax.root = (uint8_t)nr;
        ax.field = (int8_t)f;
        ax_explore_sound();
        break;
    }
    case EN_K2:
        ax.tier = (uint8_t)clampi(ax.tier + (d > 0 ? 1 : -1), 1, 3);
        ax_say(ax.tier == 1 ? "TIER 1: naturals, triads" : ax.tier == 2 ? "TIER 2: accidentals, sevenths, modes" :
               "TIER 3: everything", 1200);
        break;
    case EN_K3: ax_set_bpm(ax_bpm() + (d > 0 ? 2 : -2)); break;
    case EN_K4:
        ax.level = (uint8_t)clampi(ax.level + d * 4, 0, 127);
        ax_send_groove(ax.layers_heard);
        break;
    default: break;
    }
}

static void ax_button(uint32_t b)
{
    if (ax.settings && b != B_GLO && b != B_HOME) {
        if (b == B_FX)
            cs.ax_cfg ^= 1u;
        else if (b == B_ENV)
            cs.ax_cfg ^= 2u;
        return;
    }
    switch (b) {
    case B_HOME: ax_leave(); break;
    case B_PLAY:
        if (ax.run.on || ax.summary)
            ax_run_stop();
        else
            ax_run_begin();
        break;
    case B_REC:                                  /* the name chosen with ALGORITHM, as the answer */
        if (ax.run.on && !ax.answered && ax.shown.name_kind != 0xFF && ax.name_on && !axa_prompt_busy)
            ax_answer(ax_judge_name(&ax.shown, ax_name_opt(&ax.shown, ax.name_i)));
        break;
    case B_SCL:                                  /* hint */
        if (ax.run.on && !ax.answered) {
            ax.hinted = 1;
            ax_say("hint: it counts, but builds no layer", 1200);
        }
        break;
    case B_ARP:                                  /* the prompt again */
        if (ax.run.on && !ax.answered)
            ax_prompt_sound();
        break;
    case B_FX:
        ax.fifths ^= 1u;
        ax_say(ax.fifths ? "THE FIFTH TRANSFORM: the ring by fifths" : "the chromatic face", 1500);
        break;
    case B_ENV:                                  /* the Law of Reflection */
        if (ax.run.on)
            break;
        if (ax.mode == AXM_BOUND) {
            const ax_obj_t *o = &AX_CHORDS[ax.chord];
            int top = 0, i;
            ax_match_t m[2];
            for (i = 0; i < 12; i++)
                if ((o->mask >> i) & 1u)
                    top = i;
            if (ax_recognize(ax_mirror(o->mask, 0) == o->mask ? o->mask : ax_rot(ax_mirror(o->mask, 0), top),
                             AX_BOUND, 0, m, 2) && m[0].root == 0) {
                if (m[0].idx == ax.chord)
                    ax_say("its own mirror image", 1500);
                else
                    ax_say("MIRROR: its twin in reflection", 1500);
                ax.chord = m[0].idx;
            } else
                ax_say("its mirror has no name", 1500);
        } else if (ax.mode == AXM_FIELD || ax.mode == AXM_ORRERY) {
            int f;
            ax.fmask = ax_mirror(ax.fmask, ax.root);
            for (f = 0; f < AX_FIELDS_N && ax_rot(AX_FIELDS[f].mask, ax.root) != ax.fmask; f++)
                ;
            ax.field = (int8_t)(f < AX_FIELDS_N ? f : -1);
            ax_say(f < AX_FIELDS_N && ax_rot(AX_FIELDS[f].mask, ax.root) == ax.fmask ?
                   "MIRROR: the field reflected" : "MIRROR", 1200);
        }
        ax_explore_sound();
        break;
    case B_LFO:
        if (!ax.run.on)
            ax.shadow ^= 1u;
        break;
    case B_SEQ:
        if (!ax.run.on) {
            ax.groove ^= 1u;
            ax_explore_sound();
        }
        break;
    case B_GLO: ax.settings ^= 1u; break;
    case B_OCTDN:
    case B_OCTUP:
        cf_post(CFE_AX, AXA_STOP_ALL, 0, 0);
        ax.oct = (int8_t)clampi(ax.oct + (b == B_OCTUP ? 1 : -1), -2, 2);
        break;
    default: break;
    }
}

/* HOME held on CoralFlower's screens opens AETHER (every frame) */
static void ax_home_poll(void)
{
    if (ax.open || !cu.booted) {
        ax.home_down = 0;
        return;
    }
    if ((fm1_in.buttons >> panel.btn[B_HOME]) & 1u) {
        if (!ax.home_down)
            ax.home_down = cu.now ? cu.now : 1u;
        else if (cu.now - ax.home_down >= AX_HOME_HOLD_MS) {
            ax.home_down = 0;
            ax_enter();
        }
    } else
        ax.home_down = 0;
}

/* --------------------------------------------------------------- per frame --- */
static void ax_frame(void)
{
    uint32_t st = ax_now_step(), i;
    ax_home_poll();
    if (!ax.open)
        return;
    for (i = 0; i < AX_NQ; i++) {               /* what was queued for this sixteenth */
        ax_q_t *q = &ax.q[i];
        if (q->what == 0xFF || (int32_t)(st - q->due) < 0)
            continue;
        if (q->what == AXQ_BURST)
            ax_spawn(q->x, q->y, q->n, q->pc, AXPK_BURST);
        else if (q->what == AXQ_NOTE_ON)
            cf_post(CFE_AX, AXA_NOTE, q->note, 96);
        else if (q->what == AXQ_NOTE_OFF)
            cf_post(CFE_AX, AXA_NOTE, q->note, 0);
        else if (q->what == AXQ_HIT)
            cf_post(CFE_AX, AXA_HIT, q->note, q->x);
        q->what = 0xFF;
    }
    {
        int beat = 0;
        if ((st & 3u) == 0u && st != ax.last_beat) {
            ax.last_beat = st;
            beat = 1;
            if ((st & 15u) == 0u)
                ax.beat_ms = cu.now;             /* (the downbeat: the brackets pulse) */
        }
        ax_particles_step(cu.frame_dt, beat);
    }
    if (ax.run.on) {
        if (!ax.answered) {
            if (!ax.deadline && !axa_prompt_busy && (int32_t)(st - ax.p_step) > 1)
                ax.deadline = st + (uint32_t)ax_timeout_steps();
            if (ax.deadline && (int32_t)(st - ax.deadline) >= 0) {
                ax_say("time", 800);
                ax_answer(AXJ_WRONG);
            }
        } else if ((int32_t)(st - ax.next_step) >= 0) {
            ax_prompt_begin();
        }
    } else if (ax.answered && ax.summary && (int32_t)(st - ax.next_step) >= 0) {
        ax.answered = 0;                         /* (the run's last answer shown: the summary stays) */
    }
    if (ax.strip && (int32_t)(st - ax.strip_step) >= 0) {   /* the run over: a layer off each bar */
        ax.strip_step = st + 16u;
        if (ax.layers_heard)
            ax_send_groove(ax.layers_heard - 1);
        else
            ax.strip = 0;
    }
    if (ax.summary && (int32_t)(st - ax.summary_until) >= 0) {
        ax.summary = 0;
        ax.strip = 0;
        ax_explore_sound();
    }
    {                                            /* ambient dust: more as the form ascends */
        int want = ax.run.on ? 4 + 3 * ax.run.stage : 5, have = 0;
        for (i = 0; i < AX_NPART; i++)
            have += ax.p[i].life && ax.p[i].kind == AXPK_DUST;
        if (have < want && (ax_rand(&ax.rng) & 7u) == 0u)
            ax_spawn(8 + (int)(ax_rand(&ax.rng) % 224u), AX_Y0 + 2 + (int)(ax_rand(&ax.rng) % 160u), 1, 0,
                     AXPK_DUST);
    }
}

/* ------------------------------------------------------------------- drawing --- */
static void ax_brackets(uint8_t c)
{
    static const int X[4] = {3, 236, 3, 236}, Y[4] = {AX_Y0 + 1, AX_Y0 + 1, AX_Y1 - 2, AX_Y1 - 2};
    int i;
    for (i = 0; i < 4; i++) {
        int sx = i & 1 ? -1 : 1, sy = i & 2 ? -1 : 1;
        fb_hline(sx > 0 ? X[i] : X[i] - 9, Y[i], 10, c);
        fb_vline(X[i], sy > 0 ? Y[i] : Y[i] - 9, 10, c);
    }
}
/* the rune ticker: glyphs of no language, scrolling one a sixteenth */
static void ax_runes(int y, uint32_t step)
{
    int i, x0 = 6, n = 38;
    for (i = 0; i < n; i++) {
        uint32_t h = (uint32_t)(i + (int)(ax_motion() ? step / 4u : 0u)) * 2654435761u, b = h >> 9;
        int x = x0 + i * 6, k;
        for (k = 0; k < 12; k++)
            if ((b >> k) & 1u)
                fb_pset(x + (k % 3), y + (k / 3), (h & 7u) == 0u ? AXK_AMBER : AXK_S2);
    }
}
static void ax_text_fit(int cx, int y, const char *s, uint8_t c)   /* 2x if it fits, else 1x */
{
    char b[48];
    fb_signs(s, b, sizeof b);
    if (fb_text_w(&CF_FONT_UI, b, 2) <= 228)
        fb_text_c(&CF_FONT_UI, cx, y, b, c, 2);
    else
        fb_text_c(&CF_FONT_UI, cx, y + 6, b, c, 1);
}
static void ax_num(char *b, int v)               /* (v 0..999) */
{
    int n = 0;
    if (v >= 100)
        b[n++] = (char)('0' + v / 100 % 10);
    if (v >= 10)
        b[n++] = (char)('0' + v / 10 % 10);
    b[n++] = (char)('0' + v % 10);
    b[n] = 0;
}

static void ax_draw_settings(void)
{
    fb_rect(20, 60, 200, 96, AXK_BG);
    fb_frame(20, 60, 200, 96, AXK_S3);
    fb_text_c(&CF_FONT_UI, 120, 66, "SETTINGS", AXK_WHITE, 1);
    fb_text(&CF_FONT_UI, 32, 88, "FX   motion", AXK_GREY, 1);
    fb_text_r(&CF_FONT_UI, 208, 88, ax_motion() ? "full" : "reduced", AXK_CYAN, 1);
    fb_text(&CF_FONT_UI, 32, 106, "ENV  flash limit", AXK_GREY, 1);
    fb_text_r(&CF_FONT_UI, 208, 106, ax_flash_limit() ? "3/s" : "off", AXK_CYAN, 1);
    fb_text_c(&CF_FONT_UI, 120, 134, "GLO closes", AXK_DIM, 1);
}

static void ax_draw(void)
{
    ax_view_t v;
    uint32_t st = ax_now_step(), now = cu.now;
    int glitch = ax.glitch_until && (int32_t)(ax.glitch_until - now) > 0;
    int i;
    char b[48];
    if (ax.run.on || (ax.summary && ax.answered))
        ax_view_challenge(&v);
    else
        ax_view_explore(&v);
    fb_unclip();
    fb_fill(AXK_BG);
    /* --- the stage --- */
    fb_clip(0, AX_Y0, FB_W, AX_Y1);
    if (ax.wash_until && (int32_t)(ax.wash_until - now) > 0) {   /* a wave cleared: the root's colour, fading */
        uint32_t span = ax.wash_until - ax.wash_t0, left = ax.wash_until - now;
        if (left * 3u > span * 2u)
            fb_veil(0, AX_Y0, FB_W, AX_Y1 - AX_Y0, AXK_HUE(ax.wash_pc, 2));
        else if (left * 3u > span)
            fb_checker(0, AX_Y0, FB_W, AX_Y1 - AX_Y0, AXK_HUE(ax.wash_pc, 2));
        else
            for (i = AX_Y0; i < AX_Y1; i += 4)
                fb_hline(0, i, FB_W, AXK_HUE(ax.wash_pc, 2));
    }
    ax_tunnel(v.field, ax.fifths, ax.wash_until && (int32_t)(ax.wash_until - now) > 0 ? st * 4u : st,
              glitch && ax_motion());
    ax_particles_draw();
    if (ax.run.on && !ax.answered && ax.deadline) {   /* the countdown ring */
        int left = (int)(ax.deadline - st), tot = ax_timeout_steps();
        ax_arc(AXC_X, AXC_Y, AXC_R + 10, clampi(left * 256 / (tot ? tot : 1), 0, 256),
               left * 4 < tot ? AXK_RED : AXK_S3);
    }
    if (ax.mode == AXM_ORRERY && !ax.run.on && v.chord) {   /* the chord held, a bound state inside the field */
        ax_sig_t c = v.s;
        int x0, y0, x1, y1, a2, b2;
        c.kind = AX_BOUND;
        c.mask = v.chord;
        c.root = v.chord_root;
        c.alt = 0;
        c.shimmer = 0;
        c.quiet = 0;
        ax_sigil(&v.s);
        c.r = AXC_R;
        ax_sigil(&c);
        for (a2 = 0; a2 < 12; a2++)              /* every pair's bond, thin */
            for (b2 = a2 + 1; b2 < 12; b2++)
                if ((v.chord >> a2) & (v.chord >> b2) & 1u) {
                    ax_node_xy(AXC_X, AXC_Y, AXC_R - 18, a2, ax.fifths, &x0, &y0);
                    ax_node_xy(AXC_X, AXC_Y, AXC_R - 18, b2, ax.fifths, &x1, &y1);
                    fb_line(x0, y0, x1, y1, AXK_HUE(a2, 2));
                }
    } else
        ax_sigil(&v.s);
    if (!ax.run.on && v.s.mask && !ax.shadow)
        ax_charge(AXC_X - 14, AX_Y1 - 14, v.s.mask);
    /* the HUD: the form, its layers, the run */
    ax_form(212, 36, 16, ax.run.on || ax.summary ? ax.run.stage : 0, now);
    for (i = 0; i < AX_LAYERS; i++)
        fb_rect(197 + i * 5, 60, 4, 4, i < ax.layers_heard ? AXK_CYAN : AXK_S1);
    fb_text(&CF_FONT_MICRO, 6, AX_Y0 + 4, AXM_NAME[ax.mode], AXK_GREY, 1);
    fb_text(&CF_FONT_MICRO, 6, AX_Y0 + 11, AXM_WHAT[ax.mode], AXK_DIM, 1);
    b[0] = 'T';
    b[1] = (char)('0' + ax.tier);
    b[2] = 0;
    fb_text(&CF_FONT_MICRO, 6, AX_Y0 + 18, b, AXK_DIM, 1);
    if (ax.fifths)
        fb_text(&CF_FONT_MICRO, 18, AX_Y0 + 18, "5THS", AXK_AMBER, 1);
    if (ax.run.on || ax.summary) {
        int tot = ax.run.right + ax.run.wrong;
        str_cpy(b, "WAVE ", sizeof b);
        ax_num(b + 5, (ax.run.wave < AX_WAVES ? ax.run.wave : AX_WAVES - 1) + 1);
        ax_cat(b, "/4", sizeof b);
        fb_text(&CF_FONT_MICRO, 6, AX_Y0 + 28, b, AXK_CYAN, 1);
        str_cpy(b, "RES ", sizeof b);
        ax_num(b + 4, tot ? ax.run.right * 100 / tot : 0);
        ax_cat(b, "%", sizeof b);
        fb_text(&CF_FONT_MICRO, 6, AX_Y0 + 35, b, AXK_GREY, 1);
    }
    ax_runes(AX_Y1 - 6, st);
    ax_brackets(now - ax.beat_ms < 110u ? AXK_S3 : AXK_S2);
    if (glitch && ax_motion()) {                 /* the tear: a few bands slide sideways (the stage only) */
        uint32_t h = now * 2654435761u;
        int band;
        for (band = 0; band < 5; band++) {
            int y = AX_Y0 + (int)((h >> (band * 5)) % (uint32_t)(AX_Y1 - AX_Y0 - 8)), hh = 2 + (int)((h >> 3) & 3u),
                sh = (int)((h >> (band * 3)) % 23u) - 11, r, x;
            for (r = y; r < y + hh && r < AX_Y1; r++) {
                uint8_t *row = fb + r * FB_W, tmp[FB_W];
                for (x = 0; x < FB_W; x++)
                    tmp[x] = row[(x - sh + FB_W) % FB_W];
                for (x = 0; x < FB_W; x++)
                    row[x] = tmp[x];
            }
        }
    }
    fb_unclip();
    /* --- the top bar --- */
    fb_text(&CF_FONT_UI, 4, -1, "AETHER//SYNTAX", AXK_S3, 1);
    ax_num(b, ax_bpm());
    ax_cat(b, " BPM", sizeof b);
    fb_text_r(&CF_FONT_UI, 236, -1, b, AXK_DIM, 1);
    if ((now / 500u) & 1u)
        fb_rect(4 + fb_text_w(&CF_FONT_UI, "AETHER//SYNTAX", 1) + 2, 2, 5, 8, AXK_S3);
    fb_hline(0, 12, FB_W, AXK_S1);
    /* --- the band: the prompt, always clean --- */
    fb_hline(0, AX_BAND - 2, FB_W, AXK_S2);
    if (ax.summary && !ax.answered) {
        int tot = ax.run.right + ax.run.wrong;
        ax_text_fit(120, AX_BAND + 1, "RUN COMPLETE", AXK_WHITE);
        str_cpy(b, "right ", sizeof b);
        ax_num(b + 6, ax.run.right);
        ax_cat(b, " / ", sizeof b);
        ax_num(b + str_len(b), tot);
        ax_cat(b, "   form ", sizeof b);
        ax_num(b + str_len(b), ax.run.stage);
        ax_cat(b, "/5", sizeof b);
        fb_text_c(&CF_FONT_UI, 120, AX_BAND + 26, b, AXK_CYAN, 1);
    } else {
        uint8_t ca = ax.answered ? (ax.verdict == AXJ_RIGHT ? AXK_CYAN : AXK_RED) : AXK_WHITE;
        ax_text_fit(120, AX_BAND + 1, v.a, ca);
        if (ax.say_until && (int32_t)(ax.say_until - now) > 0)
            fb_text_c(&CF_FONT_UI, 120, AX_BAND + 26, ax.say, AXK_AMBER, 1);
        else {
            fb_signs(v.b, b, sizeof b);
            fb_text_c(&CF_FONT_UI, 120, AX_BAND + 26, b, AXK_GREY, 1);
        }
    }
    fb_text_c(&CF_FONT_MICRO, 120, AX_BAND + 42,
              ax.run.on ? "SEL HINT   ARP AGAIN   PLAY STOP" : "SELECT MODE   PLAY CHALLENGE   HOME SYNTH", AXK_DIM, 1);
    if (ax.settings)
        ax_draw_settings();
}

/* --------------------------------------------------------------- the lights --- */
static void ax_leds(void)
{
    uint8_t lit[FM1_NCOL] = {0}, mid[FM1_NCOL] = {0}, dim[FM1_NCOL] = {0}, faint[FM1_NCOL] = {0};
    uint32_t k, blink = (cu.now / 250u) & 1u, st = ax_now_step();
    uint16_t show = 0, tgt = 0;
    int root = -1, reveal = ax.reveal_until && (int32_t)(ax.reveal_until - st) > 0;
    if (ax.run.on && !ax.answered) {
        const ax_prompt_t *p = &ax.shown;
        if (ax.hinted || reveal)
            tgt = p->target;
        if (p->kind == AXP_FIELD_MISSING || p->kind == AXP_FIELD_MORPH)
            show = p->shown;
        if (p->kind == AXP_BOND_PLAY)
            cf_led(blink ? lit : mid, 14u + p->start_key, 1);
    } else if ((ax.run.on || ax.summary) && reveal)
        tgt = ax.shown.target;
    else if (!ax.run.on) {
        if (ax.mode == AXM_FIELD || ax.mode == AXM_ORRERY)
            show = ax.fmask, root = ax.root;
        else if (ax.mode == AXM_BOUND && !ax.held)
            show = ax_rot(AX_CHORDS[ax.chord].mask, ax.root), root = ax.root;
    }
    for (k = 0; k < AX_NKEYS; k++) {
        uint32_t id = 14u + k, pc = AX_KEY_PC(k);
        if ((ax.held >> k) & 1u)
            cf_led(lit, id, 1);
        else if ((tgt >> pc) & 1u)
            cf_led(blink ? lit : dim, id, 1);
        else if ((int)pc == root)
            cf_led(mid, id, 1);
        else if ((show >> pc) & 1u)
            cf_led(dim, id, 1);
    }
    cf_led(ax.run.on ? ((st & 3u) == 0u ? lit : mid) : dim, panel.btn[B_PLAY], 1);
    cf_led(ax.run.on && !ax.answered ? (ax.hinted ? lit : dim) : faint, panel.btn[B_SCL], 1);
    cf_led(ax.run.on && ax.shown.name_kind != 0xFF && !ax.answered ? (ax.name_on ? lit : dim) : faint,
           panel.btn[B_REC], 1);
    cf_led(ax.run.on && !ax.answered && (ax.shown.kind == AXP_BOND_PLAY || ax.shown.kind == AXP_FIELD_NAME) ? dim :
           faint, panel.btn[B_ARP], 1);
    cf_led(ax.fifths ? lit : dim, panel.btn[B_FX], 1);
    cf_led(ax.shadow ? lit : dim, panel.btn[B_LFO], 1);
    cf_led(dim, panel.btn[B_ENV], 1);
    cf_led(ax.groove ? lit : dim, panel.btn[B_SEQ], 1);
    cf_led(ax.settings ? lit : faint, panel.btn[B_GLO], 1);
    cf_led(mid, panel.btn[B_HOME], 1);
    cf_led(ax.oct < 0 ? lit : faint, panel.btn[B_OCTDN], 1);
    cf_led(ax.oct > 0 ? lit : faint, panel.btn[B_OCTUP], 1);
    for (k = 0; k < FM1_NCOL; k++) {
        fm1_led_faint[k] = faint[k];
        fm1_led_dim[k] = dim[k];
        fm1_led_dim2[k] = mid[k];
        fm1_led[k] = lit[k];
    }
}
