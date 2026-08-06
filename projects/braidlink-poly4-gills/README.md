# Braidlink Poly4 for Ksoloti Gills

Braidlink Poly4 is the separate four-voice version of Braidlink. It preserves
the same models, Gills controls, MIDI channel behavior, and CC20-29 mapping
without modifying or depending on the monophonic project.

Each voice owns its oscillator, Streams vactrol articulation, envelope,
low-pass state, soft drive, and tuned modal body. The four voices are mixed
with equal-power normalization and then sent through one shared Clouds stereo
reverb. Inactive voices do not run their DSP path.

Open `braidlink-poly4-gills.axp` in Ksoloti Patcher 1.1.0. Keep all five files
in this folder together.

## Polyphony behavior

- Four simultaneous notes are supported.
- Repeating an already-held note retriggers its existing voice.
- When all four voices are held, a new note steals the least recently used
  voice.
- Note-off messages release the matching voice in gate mode. Pluck mode uses
  its own decay while still accepting note-off state.
- A fixed 32-entry note-event queue preserves chord note-ons that arrive before
  the next 16-sample audio block. `OV` on the lower-right of the OLED indicates
  that this queue has overflowed.
- Pitch bend is global and has a fixed range of plus or minus two semitones.
- CC120 and CC123 release all held voices.

Digitakt parameter locks are global/live MIDI CC changes. They affect every
currently sounding voice; they are not captured separately for each note.

## Gills controls

| Control | Function |
| --- | --- |
| Pot 1 | Timbre |
| Pot 2 | Color |
| Pot 3 | Attack |
| Pot 4 | Decay |
| Pot 5 | Vactrol brightness |
| Pot 6 | Modal body amount |
| Pot 7 | Modal body damping |
| Pot 8 | Soft drive |
| Pot 9 | Shared reverb space |
| Pot 10 | Output level |
| Button 1 | Cycle `MORP`, `FOLD`, `FM`, and `HARM` for all voices |
| Button 2 | Cycle `CLN`, `WOOD`, and `METL` for all voices |
| Button 3 | Toggle MIDI CC offset and absolute modes |
| Button 4 | Toggle the shared Clouds reverb |
| Encoder | MIDI channel 1-16 |
| Encoder push | Toggle pluck and gate articulation |

The first OLED line shows `V0` through `V4` for active DSP voices. The fourth
line shows the number of currently held notes. The LEDs show held-note gate,
modal-body active, MIDI offset mode, and reverb enabled.

## Digitakt II MIDI map

On the Digitakt II MIDI machine, set `CHAN` to the channel shown on the Gills
OLED. Set FLTR `SEL1-SEL8` to CC20-27 and AMP `SEL9-SEL10` to CC28-29:

| CC | Suggested name | Destination |
| --- | --- | --- |
| 20 | `TIMB` | Timbre |
| 21 | `COLR` | Color |
| 22 | `ATK` | Attack |
| 23 | `DEC` | Decay |
| 24 | `BRIT` | Vactrol brightness |
| 25 | `BODY` | Modal body amount |
| 26 | `DAMP` | Modal damping |
| 27 | `DRIV` | Soft drive |
| 28 | `SPCE` | Shared reverb amount |
| 29 | `LEVL` | Output level |

Activate the corresponding value controls with `FUNC` plus the data-entry
knob and start them at 64. Offset mode is the default: CC64 is neutral around
the Gills pot position. Absolute mode maps CC0-127 across the full range.

Digitakt II `NOT2-NOT4` offsets on TRIG page 1 can send a four-note chord from
one trig. Separate MIDI tracks may also share the same channel.

## Proof-of-concept boundaries

Four-voice host stress and a successful target compile cannot establish the
STM32F427's real-time headroom. Test the worst case on hardware: four held
`HARM` voices, `METL` body, reverb enabled, and dense CC locks. Also verify
voice stealing, DIN versus USB chord timing, output level, and transitions
between global models and body modes before treating this as performance-ready.
