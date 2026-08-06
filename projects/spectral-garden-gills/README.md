# Spectral Garden for Ksoloti Gills

Spectral Garden is a six-band resonator and filter-bank instrument. Each band
occupies one of twenty notes in a selected scale. Rotate moves the six-note
shape through the scale, Spread changes the distance between its notes, and
Resonance moves from broad filtering into long pitched ringing.

The instrument can process the stereo inputs or excite itself. `PULSE` creates
short, internally clocked noise strikes. `AIR` feeds the resonators with a low
continuous noise floor and occasional stronger gusts. `INPUT` routes alternate
bands from the left and right audio inputs. Encoder pushes and manual mutations
can strike the resonators in every source mode.

Motion is deliberately bounded. It changes the rotation, the band receiving the
strongest strike, and the route and depth of the resonators' interaction. It
does not rewrite the scale, root, spread, resonance, or six energy settings.
Holding motion freezes the chord while internally clocked strikes and spectral
interaction continue across its bands.

Open `spectral-garden-gills.axp` in Ksoloti Patcher 1.0.12. Keep all five files
in this folder together.

## Gills controls

| Control | Function |
| --- | --- |
| Pots 1-6 | Energy of resonator bands 1-6; each also excites, opens, and subtly detunes neighboring bands |
| Pot 7 | Rotate through the twenty scale slots |
| Pot 8 | Spread between neighboring bands, 1-5 slots |
| Pot 9 | Resonance / ringing time |
| Pot 10 | Motion rate, tuning glide speed, and interaction depth; fully left stops automatic events |
| Button 1 | Cycle `PULSE`, `AIR`, and `INPUT` excitation |
| Button 2 | Mutate the rotation once and strike |
| Button 3 | Hold or release automatic pitch movement |
| Button 4 | Cycle `MAJOR`, `MINOR`, `PENTATONIC`, `HARMONIC`, `JUST`, `CHROMATIC`, and `TRITAVE` scales |
| Encoder | Root note from C1 to C4 |
| Encoder push | Manual strike |

LED 1 flashes on a strike. LED 2 indicates `AIR`, LED 3 indicates held
movement, and LED 4 indicates `INPUT`. With LEDs 2 and 4 off, `PULSE` is active.

The display shows the root, scale, source, movement state, the six active slot
numbers, and the Rotate, Spread, Resonance, and Motion values.

## Starting point

Raise Pots 1-6 to form a chord, set Resonance around 70%, Spread around the
middle, and Motion around 25%. `PULSE`, C2, and the major scale are the startup
state. The top row, Pots 1-5, controls five interacting resonator energies; Pot
6 begins the bottom row. Moving any energy pot creates a short gesture that
spreads to adjacent and occasionally more distant bands. Turn Motion fully left
for manual strikes and gentler coupling. Select `INPUT` to use the instrument as
a wet-only stereo filter bank; the patch configures both inputs at 0 dB gain.

High resonance intentionally produces long overlapping tails. Large level
settings or loud input can drive the final soft limiter and make the stereo
image denser.

## Design and validation boundary

The resonator, scale tables, excitation patterns, and generative motion are a
new Ksoloti-native implementation. The interaction is inspired by rotating
scale-quantized resonator instruments, including the 4ms Spectral Multiband
Resonator, but no 4ms source code or scale tables are included.

`spectralgarden_dsp.h` uses the Mutable Instruments `stmlib` semitone-ratio
helper already distributed with Ksoloti under its MIT license. Hardware
listening is required to verify real-time headroom, input gain, output level,
noise behavior, and the musical pacing of Motion before installing the patch
for startup use.

## Validation

Initial Gills testing exposed internal excitation and post-resonator output
levels that were far below a practical listening range. A later listening pass
found the upper-row band controls too static. The current interaction network
keeps the percussive resonator core and final limiter while making band energy
changes propagate through neighboring levels, short gesture strikes, bounded
micro-detuning, and a motion-controlled feedback route.

XML validation, host C++ syntax compilation, and a temporary behavioral and
level stress harness passed. The harness covers button debounce, all excitation
modes, manual and automatic movement, held-chord behavior, individual movement
of every upper-row pot, output bounds, explicit RMS/peak thresholds, and 50,000
rapid control-and-strike processing blocks. The temporary harness is not part
of the project.

Ksoloti Patcher 1.0.12 completed the Cortex-M4 compile and link. Its target
report was 14,688 bytes of text, 92 bytes of data, 1,912 bytes of BSS, and
16,692 bytes total. Offline checks do not establish real-time CPU safety or
replace listening on Gills.
