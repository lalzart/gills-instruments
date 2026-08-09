# Knob Loom Shadow for Ksoloti Gills

Knob Loom Shadow is a separate eight-thread sibling of Knob Loom. Pots 1-8
retain the original relationship: absolute position selects a scale degree,
while physical movement bows or plucks that thread. Pots 9 and 10 operate a
contrasting negative-rhythm layer made from two slow interacting waves.

The patch remains silent at startup. Shadow does not contain a hidden drum
sequence or free-running audible oscillator. It can only cut and re-release
energy after the player has moved a thread pot or performed a strum.

Open `knob-loom-shadow-gills.axp` in Ksoloti Patcher 1.0.12 and keep all five
files in this folder together.

## Gills controls

| Control | Function |
| --- | --- |
| Pots 1-8 | Tune and physically excite threads 1-8 |
| Pot 9 `TENSION` | Move the two shadow waves from slow near-locking toward faster, more agitated interference |
| Pot 10 `APERTURE` | Set the width, depth, and duration of shadow intersections; fully left disables Shadow |
| Button 1 | Strum all eight threads and reset the shadow waves to a common downbeat |
| Button 2 | Reverse coupling, strum direction, and shadow travel |
| Button 3 | Toggle long sustain |
| Button 4 | Cycle `RING`, `STAR`, `PAIR`, and `XCRS` coupling and shadow travel |
| Encoder | Thread decay |
| Encoder push | Cycle `MAJOR`, `PENTATONIC`, `DORIAN`, `HARMONIC MINOR`, and `WHOLE TONE` |

LED 1 shows thread, strum, or Shadow activity. LEDs 2 and 3 indicate Reverse
and Sustain. LED 4 flashes for each Shadow event. The OLED shows the eight
thread slots, Tension, Aperture, scale, decay, direction, and topology.

## What Shadow does

Two inaudible triangle waves run at related but unequal speeds. `TENSION`
raises their speed and separation. `APERTURE` defines a window around their
intersections. When the waves enter that window, the thread field receives a
rounded dip rather than a hard mute. The removed energy charges a short
high-passed noise envelope, heard as a papery shuttle tick that travels through
the current topology.

Narrow Aperture settings create isolated pinpricks. Wider settings make longer,
deeper holes and more persistent clusters. Shadow is deterministic and bounded,
but the relationship between its waves keeps the rhythm from behaving like a
fixed step sequence.

The thread topology also shapes the shuttle path. `RING` crosses the panel in
order, `STAR` returns repeatedly to the two center threads, `PAIR` walks through
adjacent partners, and `XCRS` jumps between opposite sides.

## Design boundary

The original ten-thread Knob Loom remains a separate unchanged instrument.
Shadow uses eight copies of the same two-mode thread voice, freeing four modal
resonators for the contrasting layer and reducing the density of sustained
chords. The Shadow core uses phase accumulators, triangle waves, a window
comparison, a smoothed gain envelope, and one filtered-noise tick; it does not
add another pitched oscillator bank.

`knobloom_shadow_dsp.h` uses the Mutable Instruments `stmlib` semitone-ratio
helper distributed with Ksoloti under its MIT license. The Shadow engine is an
original implementation. Window-comparator rhythm is a conceptual modular
synthesis influence, not copied source code.

Offline compilation and host tests cannot establish real-time CPU headroom,
ADC-noise immunity, output level, or musical balance. Test with **Live** before
choosing any startup installation.

## Validation

Repository XML and host C++ validation passed for all eleven sibling
instruments. A temporary behavioral harness confirmed selective single-pot
excitation, all eight strum voices, debounced buttons, topology and scale
changes, finite output, and 50,000 randomized control blocks.

With Aperture fully open, 75 internal wave intersections over eight silent
seconds still produced exact-zero audio. Shadow therefore does not create an
autonomous startup beat. Over a ten-second active comparison, high Tension
produced 157 intersections versus 6 at low Tension. In a six-second Aperture
comparison, the narrow window occupied 972 of 18,000 blocks and the wide window
occupied 7,138 blocks; their minimum field gains were 0.755 and 0.177. The
stress run measured -19.00 dBFS RMS and -4.85 dBFS peak with no samples at the
final limiter threshold. The temporary harness is not part of the project.

Ksoloti Patcher 1.0.12 completed Cortex-M4 code generation, compilation, and
linking on two validation passes. Its target report was 14,632 bytes of text,
40 bytes of data, 2,072 bytes of BSS, and 16,744 bytes total. No USB Core was
available, so hardware CPU/deadline behavior, levels, ADC response, and audible
Shadow balance remain unverified.
