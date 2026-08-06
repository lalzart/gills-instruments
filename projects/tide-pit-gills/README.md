# Tide Pit for Ksoloti Gills

Tide Pit is a deeper, grungier sibling to Tidepool. It preserves the four-stage
generative gesture and feedback-body character, but uses a 16-bit internal
waveguide, a sympathetic lower string, eight-mode stereo bodies, six
high-quality grains, and a true diffusion reverb tail.

The instrument runs continuously. Button 1 cycles three oscillator sources:

- `REED` is the original Tide Pit feedback waveguide and remains the default.
- `RND` is a softer sine/triangle source.
- `FOLD` uses the Mutable Instruments Braids sine-fold transfer function.

Pot 7 shapes material on `REED`, triangle color on `RND`, and fold depth on
`FOLD`.

Button 4 taps through three post-reverb effect modes. `CLEAN` is the original
Tide Pit signal path and remains the default, `FILT` is a resonant low-pass
filter, and `DRIVE` is the asymmetric saturation stage from Palimpsest. Pot 9
and Pot 10 remember separate values in each mode and use soft pickup when you
return to a mode. Hold Button 4 to freeze or resume the granular buffer.

Tide Pit is a separate instrument. It does not replace or modify Tidepool.

Open `tidepit-gills.axp` in Ksoloti Patcher. Keep the `.axp`, `.axo`, and both
`.h` files together; the custom object is local to the patch.

## Controls

| Control | Function |
| --- | --- |
| Pots 1-4 | Stage values |
| Pot 5 | Cycle rate |
| Pot 6 | Memory / mutation frequency |
| Pot 7 | Feedback-body material and damping |
| Pot 8 | Grain position |
| Pot 9 | CLEAN: grain size; FILT: cutoff; DRIVE: tone |
| Pot 10 | CLEAN: grain depth/spread/tail; FILT: resonance; DRIVE: amount |
| Button 1 | Cycle REED, RND, and FOLD oscillators |
| Button 2 | Mutate once |
| Button 3 | Lock automatic mutation |
| Button 4 tap | Cycle CLEAN, FILT, and DRIVE effects |
| Button 4 hold | Freeze / resume the grain buffer |
| Encoder | Root note (C2-C5) |
| Encoder push | Change scale |
| Encoder hold | Change wave destination: pitch, body, grain, or all |

The LEDs indicate the current stage. The display shows the root, stage values,
active Pot 9/10 values and effect mode, scale, modulation destination,
oscillator, and lock/freeze state.

The richer voice consumes more CPU than Tidepool. Test DSP load and output
level on hardware before installing it as a startup patch.
