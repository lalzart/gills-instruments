# Tidepool for Ksoloti Gills

Tidepool is a four-stage generative instrument with a physical-model voice,
wooden resonator, sub oscillator, and 12-voice granular layer.

The instrument runs continuously. Button 1 cycles three oscillator sources:

- `FLUT` is the original Tidepool physical-model voice and remains the default.
- `RND` is a softer sine/triangle source.
- `FOLD` uses the Mutable Instruments Braids sine-fold transfer function.

Pot 7 shapes breath/body color on `FLUT`, triangle color on `RND`, and fold
depth on `FOLD`.

Open `tidepool-gills.axp` in Ksoloti Patcher. Keep the `.axp`, `.axo`, and `.h`
files together; the custom object is local to the patch.

## Controls

| Control | Function |
| --- | --- |
| Pots 1-4 | Stage values |
| Pot 5 | Cycle rate |
| Pot 6 | Memory / mutation frequency |
| Pot 7 | Voice timbre |
| Pot 8 | Grain position |
| Pot 9 | Grain size |
| Pot 10 | Grain density, spread, and mix |
| Button 1 | Cycle FLUT, RND, and FOLD oscillators |
| Button 2 | Mutate once |
| Button 3 | Lock automatic mutation |
| Button 4 | Freeze / resume the grain buffer |
| Encoder | Root note (C2-C5) |
| Encoder push | Change scale |
| Encoder hold | Change modulation destination |

The LEDs indicate the current stage. The display shows the root, stage values,
memory, grain amount, scale, modulation destination, oscillator, and
lock/freeze state.

The granular buffer uses about 192 KB of SDRAM. Test DSP load and output level
on hardware before installing the patch for normal use.
