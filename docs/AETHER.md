# AETHER//SYNTAX

A music-theory toy for the M-VAVE FM-1, built into CoralFlower. Every note, interval, scale and chord is a set of
points on a ring of twelve: the **Lattice**. Each has a sigil drawn from that one rule, so the pictures show how the
theory fits together. Nothing is saved between sessions except three settings.

AETHER//SYNTAX opens at power-on, in LEARN. **HOME** goes to CoralFlower, the chord synth. On CoralFlower, **hold HOME** for
about a second to come back.

## LEARN: the lessons

LEARN is the mode AETHER opens in. It's a course of 36 lessons, from the first three white keys to altered
chords and symmetric scales:

| Lessons | What they teach |
| --- | --- |
| 1–3 | note names: C D E, then F G A B, then the black keys |
| 4–6, 10, 17 | intervals to play: steps, thirds, 4ths and 5ths, 6ths and the octave, 7ths and the tritone |
| 7, 12–13 | the major and natural minor scales, the pentatonics |
| 8, 14–15 | triads: major, minor, diminished, augmented, sus2, sus4 |
| 9, 11, 16, 18, 22 | by ear: triads, steps, more triads, every interval, seventh chords |
| 19–21 | seventh chords: maj7, 7, m7, m7♭5, dim7, mMaj7 |
| 23–27 | the modes: play them, find their missing node, morph major into each, name them by ear |
| 28–29 | blues, harmonic minor, melodic minor (and the three minors by ear) |
| 30–34 | 6ths and add9, ninths, altered dominants, colour chords, 11ths and 13ths |
| 35 | symmetric scales: whole-tone, both diminished, augmented |
| 36 | final: everything, intervals in both directions |

**How a lesson runs.** Each new card (a note, an interval, a chord, a scale, a mode change) is *taught* first. The
screen shows it, the keys light up, you hear it, and you play it once (or, for an ear card, press REC on its
name). Then it is *asked*, mixed with review of everything from earlier lessons, with no lights. About a third of
prompts are review. A lesson is mastered when each of its own cards has been answered right enough times in a row
at 75% or better. The screen then says LESSON MASTERED and shows a report. PLAY goes on to the next lesson.

**Spaced repetition** happens within the session, counted in prompts rather than days. A card's box (0–5) sets when
it comes back (after 2, 4, 7, 13, 24 or 40 prompts). A right answer moves it up, and a quick first answer skips a
box. A hinted or slow answer keeps it where it is. A miss drops it to box 0 and asks it again two prompts later.

**The tutor** keeps track of misses, accuracy and answer time for every card, and which answers you confuse. After a
miss it shows that card's tip: what it is, its formula, what it sounds like. The report (PLAY during a lesson)
lists the three cards to work on, the pair you mix up most ("you mix up m7 and m7♭5"), or the earlier lesson to go
back to when its cards keep failing.

**No persistence:** nothing is saved, so pick your lesson with ALGORITHM next time. Cards from lessons before the one
you pick start as "presumed known" and come up now and then. Missing one tells the tutor otherwise.

| Control | Choosing a lesson | In a lesson |
| --- | --- | --- |
| ALGORITHM | lesson 1–36 | pick a name (ear cards) |
| PLAY | start it | stop: the report |
| REC | — | answer with the name chosen |
| SEL | — | hint (lights the answer; counts, but doesn't advance the card) |
| ARP | — | hear it again |

## Themes

GLO opens settings, and ALGORITHM picks one of 13 themes (saved): REZ, PHOSPHOR, AMBER, VAPOR, BLOOD MOON, ICE, SOLAR,
MONO, NEON TOKYO, ABYSS, PAPER (light), FOREST, SUNSET. Every theme except MONO keeps the twelve note colours a twelfth
of the colour wheel apart, so colour still tells you pitch.

## The idea

| Musical object | Nodes lit | Cosmic name | Drawn as |
| --- | --- | --- | --- |
| Note | 1 | Particle | a node with a spoke and orbit rings |
| Interval | 2 | Bond | a line across the ring, ticks for its class (1–6), an arrow for its direction |
| Chord | 3–6 | Bound state | a hatched polygon |
| Scale / mode | 5–8 | Field | an outline polygon with spokes in |
| Chromatic | 12 | The Divine All | the ring itself |

The laws you can try on the FM-1 are all true music theory:

| Law | Control | What it shows |
| --- | --- | --- |
| Rotation | PRESETS | transposing keeps the shape: C major turns into D major |
| Anchor | K1 | modes share one polygon; only the root moves (C Ionian → D Dorian → E Phrygian …) |
| Reflection | ENV | a field mirrored about its root (Ionian ↔ Phrygian, Dorian is its own mirror); a chord mirrored inside its span (major ↔ minor; maj7 and m7 are their own mirrors) |
| Void | LFO | the dark nodes lit: C major's shadow is G♭ major pentatonic |
| Containment | (BOUND STATE) | how many seven-note fields hold the chord you play |
| Fixed points | — | whole-tone, diminished, augmented and dim7 shimmer: some rotations map them onto themselves |
| Fifth Transform | FX | the ring reordered by fifths: a diatonic field becomes seven neighbours in a row |

The catalog (12 notes, 6 bond classes, 17 fields, 25 chords, with their cosmic names) is `assets/lattice.txt`;
`tools/gen_lattice.py` turns it into `build/gen/ax_lattice.h` at build time.

## Controls

| Control | EXPLORE | CHALLENGE |
| --- | --- | --- |
| Keys F3–G5 | play; the screen names what you play | answer |
| SELECT | mode: PARTICLE, BOND, FIELD, BOUND STATE, ORRERY, LEARN | — |
| PRESETS | root | — |
| ALGORITHM | the field (FIELD, ORRERY) or chord (BOUND STATE) shown | pick a name to answer with |
| K1 | anchor (FIELD, ORRERY) | — |
| K2 | tier 1–3 | tier 1–3 (next run) |
| K3 | tempo 80–160 BPM (saved) | tempo |
| K4 | groove level | groove level |
| PLAY | start a challenge | stop it |
| REC | — | answer with the name ALGORITHM is on |
| SEL | — | hint (counts, builds no layer) |
| ARP | — | hear the prompt again |
| FX | fifths face on / off | fifths face |
| ENV | mirror | — |
| LFO | shadow on / off | — |
| SEQ | a quiet groove on / off | — |
| GLO | settings: ALGORITHM theme, FX reduced motion, ENV flash limit (all saved) | settings |
| OCT− / OCT+ | octave of your notes, ±2 | octave |
| HOME | CoralFlower | CoralFlower |

The key lights help as well: a field's or chord's notes glow dimly with the root brighter, a bond's start key
blinks, a hint blinks the answer's keys, and after a miss the right keys light for a bar.

## Modes

| Mode | EXPLORE | CHALLENGE prompts |
| --- | --- | --- |
| PARTICLE | each key's particle, name, octave and cosmic name | FIND a note by name; from tier 2 wave 3 on, by its sigil only |
| BOND | hold a key and strike another: the bond, its name and direction | hear a bond from a lit start key and play its other end, or name it (ALGORITHM + REC). Waves widen from half and whole steps to every interval; descending ones from tier 2 |
| FIELD | browse fields, rotate, anchor, mirror, shadow, flip faces | MISSING NODE (play the dark one), MORPH (play only the notes that change between two modes on one root), NAME IT (hear it, name it) |
| BOUND STATE | play any chord: name, twins (C6 = Am7), containing fields; no keys: browse the catalog | PLAY a chord symbol, any voicing or inversion |
| ORRERY | a field, the chord you hold inside it, every bond between its notes | — |

A run is 4 waves of 8 prompts, each prompt with a countdown ring (shorter at higher tiers). A right answer locks on:
its nodes fire one per sixteenth, and a groove layer is added. A miss tears the screen for half a bar, shows the
answer, and takes a layer away. When the run ends, the summary shows and the groove strips back one layer per bar.

## The Rez layer

- **On the grid:** effects and UI sounds land on sixteenths at the session tempo. The notes you play sound at once.
- **The groove:** kick, hats, bass, backbeat clap, arpeggio, lead. The lead plays your last right notes back.
  During a challenge it never follows the answer, and bond and field prompts play with the melodic layers
  muted. A run starts with a soft heartbeat until the first layer comes in.
- **Feedback is unpitched:** an open hat on a right answer, two rims on a miss, a tom fill on a wave clear. No
  interval sounds except the one being taught.
- **Ascension:** the corner form grows from point to line, triangle, tetrahedron and octahedron as layers build. A
  full wave with all six layers held turns it into a gold icosahedron, whose 12 vertices are the 12 notes. It resets
  at power-off.
- **The tunnel** takes the shape of the field on screen. During a prompt it is a neutral 12-gon, so it never gives
  the answer away.
- **Safety:** with the flash limit on (the default), full-screen washes happen at most 3 times a second and fade
  rather than strobe. Reduced motion stops the tunnel, the tear and the drifting runes.

## For developers

| File | What |
| --- | --- |
| `assets/lattice.txt`, `tools/gen_lattice.py` | the catalog → `build/gen/ax_lattice.h` (masks, names, interval vectors, symmetry, a sine table) |
| `firmware/src/ax_core.c` | the theory: masks, recogniser, laws, prompts, judging, runs. No HAL |
| `firmware/src/ax_learn.c` | LEARN: the curriculum, spaced repetition, the tutor. No HAL |
| `firmware/src/ax_audio.c` | the audio ISR side: notes, prompts, groove, hits (included by `cf_out.c`) |
| `firmware/src/ax_ui.c` | screen, input, lights (included at the end of `cf_screens.c`) |
| `tests/ax_core_test.c` | 23,000+ checks of the theory |
| `tests/ax_learn_test.c` | the curriculum and scheduler with simulated learners (perfect, confused, forgetful, 80%) |
| `tests/ax_headless.c` | the whole firmware played through every mode's challenge, with screenshots; `AX_DEMO=DIR` writes an animation's frames |

Hooks in CoralFlower are small: `cf_ui.c` hands keys, buttons, encoders, LEDs and the frame to AETHER while
`ax_open()`; `cf_screens.c` lets it draw; `cf_out.c` adds one event op (`CFE_AX`) and one tick. Settings live in the
record's former padding bytes (`cs.ax_cfg` at offset 181, `cs.ax_theme` at 159), so the record's size, backups and downgrades are unchanged.
`AX_BOOT=0` builds CoralFlower's HOME at power-on, which is what CoralFlower's own UI tests use.

The panel moves a full screen in ~77 ms and only changed 16×16 tiles are sent, so motion steps on the beat. Measured
in the headless runner: ~360 tiles/s idle, ~900 tiles/s in a run with the full groove, ~120 with reduced motion.

```sh
python3 tools/build.py --gen-only
cc -std=c99 -Wall -Wextra -Wno-unused-function -Ibuild/gen -Ifirmware/src -o build/host/ax_core_test tests/ax_core_test.c && build/host/ax_core_test
cc -O2 -std=gnu11 -w -Itools/emu/web/compat -Ibuild/gen -Ifirmware/src -DEMU_WEB -o build/host/ax_headless tests/ax_headless.c -lm && build/host/ax_headless build/ax-run
./build.sh --release X.Y        # the .fwsc: needs JieLi's toolchain (Linux x86-64), or the release workflow on GitHub
```
