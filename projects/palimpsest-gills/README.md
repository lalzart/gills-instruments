# Palimpsest for Ksoloti Gills

Palimpsest is a four-stage modal instrument. Each stage triggers a direct
strike and three delayed, scale-aware traces from a shared 16-voice pool.

The instrument runs continuously. Button 1 cycles four voice models:

- `TRACE` is the original three-partial modal voice and remains the default.
- `KNOCK` adds a short, woody excitation and tightly damped upper modes.
- `SKIN` uses membrane-like partials with a brief downward pitch bend.
- `SHARD` emphasizes bright, inharmonic partials and a brittle transient.

Existing notes and scheduled traces retain the voice that created them, so a
new voice can overlap the previous tail naturally.

Open `palimpsest-gills.axp` in Ksoloti Patcher. Keep the `.axp`, `.axo`, and
`.h` files together; the custom object is local to the patch.

## Controls

| Control | Function |
| --- | --- |
| Pots 1-4 | Stage pitches |
| Pot 5 | Cycle rate |
| Pot 6 | Memory / mutation frequency |
| Pot 7 | Harmonic-to-inharmonic material |
| Pot 8 | Trace spacing |
| Pot 9 | CLEAN: trace decay; FILT: cutoff; DRIVE: tone |
| Pot 10 | CLEAN: trace activity/mix; FILT: resonance; DRIVE: amount |
| Button 1 | Cycle TRACE, KNOCK, SKIN, and SHARD voices |
| Button 2 | Mutate once |
| Button 3 | Lock automatic mutation |
| Button 4 tap | Cycle CLEAN, FILT, and DRIVE effects |
| Button 4 hold | Clear voices and scheduled traces |
| Encoder | Root note (C2-C5) |
| Encoder push | Change scale |

`CLEAN` is the original signal path and startup default. `FILT` is a resonant
stereo low-pass filter. `DRIVE` adds tone-shaped asymmetric saturation. Each
mode remembers its Pot 9 and Pot 10 settings. When returning to a mode, a knob
uses soft pickup and does not change the stored value until it crosses it.

The LEDs indicate the current stage. The display shows the selected voice and
effect, root, stage values, scale, lock state, and the parameters relevant to
the active effect.

Test the patch on hardware before installing it as a startup patch. DSP load
and output level have not been characterized across all settings.
