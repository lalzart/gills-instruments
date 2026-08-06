# Knob Loom for Ksoloti Gills

Knob Loom turns the Gills panel itself into a ten-string instrument. Every pot
sets the scale position of one modal thread, but pot movement is also the
exciter: a slow movement bows the thread and a fast flick plucks it. The patch
does not begin with a hidden sequence or drone. It waits for touch, while
Button 1 can strum the complete loom at any time.

The threads exchange a small, bounded amount of energy. Their routing can be a
directed ring, a two-center star, five pairs, or cross-panel connections.
Reversing the flow changes which already-ringing thread influences the next
one without retuning or resetting any voice.

Open `knob-loom-gills.axp` in Ksoloti Patcher 1.0.12 and keep all five files in
this folder together.

## Gills controls

| Control | Function |
| --- | --- |
| Pots 1-10 | Tune and physically excite threads 1-10 |
| Button 1 | Strum all ten threads |
| Button 2 | Reverse coupling and strum direction |
| Button 3 | Toggle long sustain |
| Button 4 | Cycle `RING`, `STAR`, `PAIR`, and `XCRS` coupling |
| Encoder | Thread decay |
| Encoder push | Cycle `MAJOR`, `PENTATONIC`, `DORIAN`, `HARMONIC MINOR`, and `WHOLE TONE` |

LED 1 shows movement or strum activity. LEDs 2 and 3 indicate reverse and
sustain. LED 4 indicates that a non-ring topology is active. The display shows
the ten selected scale slots as a compact row.

## Playing it

Begin with the pots spread across the panel and decay near the middle. Flick a
single pot, then move a neighboring one slowly while the first thread rings.
Use `PAIR` for clear duets, `RING` for directional ripples, and `XCRS` for the
least predictable stereo exchanges. Sustain is intentionally capable of dense
overlap, but the coupling and final output remain bounded.

Knob movement is derived from filtered ADC motion. Hardware testing must
determine whether the movement threshold feels responsive on a physical Gills
panel without reacting to idle pot noise.

## Design and validation boundary

The modal thread bank, movement excitation, strum scheduler, and coupling
topologies are a new Ksoloti-native implementation. The header uses the
`stmlib` semitone-ratio helper distributed with Ksoloti under its MIT license.

Offline compilation and host tests cannot establish real-time CPU headroom,
ADC-noise immunity, output level, or musical feel. Test the patch with **Live**
before selecting any startup installation.

## Validation

XML validation and host C++ syntax compilation passed. A temporary behavioral
harness confirmed silent idle startup, selective excitation from a single pot,
ten-thread strumming, debounced direction/sustain/topology/scale controls,
bounded output, and a 50,000-block randomized movement and decay stress run.
The fast single-pot gesture peaked at -25.43 dBFS. The deliberately dense
sustained stress case measured -7.82 dBFS RMS and -0.97 dBFS peak with 0.415%
final limiter engagement. The temporary harness is not part of the project.

Ksoloti Patcher 1.0.12 completed Cortex-M4 code generation, compilation, and
linking on two validation passes. Its target report was 13,648 bytes of text,
40 bytes of data, 2,180 bytes of BSS, and 15,868 bytes total. The board was not
accessible for Live testing, so hardware CPU headroom and audible behavior
remain unverified.
