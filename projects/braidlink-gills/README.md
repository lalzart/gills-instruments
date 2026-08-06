# Braidlink for Ksoloti Gills

Braidlink is a monophonic MIDI instrument designed as a direct companion for
the Elektron Digitakt. Gills owns the base sound and manual modes; the Digitakt
supplies notes, velocity, length, pitch bend, live CC movement, and parameter
locks. MIDI note length shapes the voice in gate mode; pluck mode deliberately
uses its own decay.

The oscillator is a compact Braids-derived macro voice with four manually
selected models. Mutable Instruments Streams supplies the vactrol response,
the custom modal body is conceptually inspired by Rings, and a restrained
Clouds reverb supplies stereo space.

Open `braidlink-gills.axp` in Ksoloti Patcher 1.1.0. Keep all files in this
folder together.

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
| Pot 9 | Reverb space |
| Pot 10 | Output level |
| Button 1 | Cycle `MORP`, `FOLD`, `FM`, and `HARM` oscillator models |
| Button 2 | Cycle `CLN`, `WOOD`, and `METL` body modes |
| Button 3 | Toggle MIDI CC offset and absolute modes |
| Button 4 | Toggle Clouds reverb |
| Encoder | MIDI channel 1-16 |
| Encoder push | Toggle pluck and gate articulation |

The four LEDs show MIDI gate, modal-body active, MIDI offset mode, and reverb
enabled.

## Digitakt setup and MIDI map

Assign a MIDI machine to a Digitakt track, set its channel to match the channel
shown on Braidlink's display, and assign the following CC numbers:

| CC | Suggested Digitakt name | Destination |
| --- | --- | --- |
| 20 | `TIMB` | Timbre |
| 21 | `COLR` | Color |
| 22 | `ATK` | Attack |
| 23 | `DEC` | Decay |
| 24 | `BRIT` | Vactrol brightness |
| 25 | `BODY` | Modal body amount |
| 26 | `DAMP` | Modal damping |
| 27 | `DRIV` | Soft drive |
| 28 | `SPCE` | Reverb amount |
| 29 | `LEVL` | Output level |

Offset mode is the default. CC 64 is neutral, and values below or above 64
move around the corresponding Gills pot position. Absolute mode maps CC 0-127
directly across the full parameter range. Until a CC has been received, the
Gills pot alone controls that parameter.

Pitch bend has a fixed range of plus or minus two semitones. MIDI CC 120 and
123 are treated as all-notes-off commands.

## Proof-of-concept boundaries

Braidlink intentionally starts with one voice and one active oscillator path.
It does not switch oscillator models over MIDI, allocate memory in the audio
path, or run inactive oscillator models in parallel. Model and body changes
are manual Gills actions.

The instrument still requires hardware listening and timing checks before it
should be treated as performance-ready. In particular, verify DIN versus USB
MIDI timing, maximum parameter-lock traffic, output level, real-time DSP load,
and the transitions between oscillator and body modes.
