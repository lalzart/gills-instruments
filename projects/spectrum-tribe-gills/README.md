# Spectrum Tribe for Ksoloti Gills

Spectrum Tribe is a special-interest polyrhythmic percussion instrument. It
turns six tuned resonators into three drum voices: a low body, a mid wooden
voice, and a high rim or metallic voice. Each drum uses one resonator for its
body and a second for a related overtone.

The three voices share a four-beat cycle but choose their own number of evenly
distributed hits. Settings such as 3, 4, and 5 therefore produce a continuous
3:4:5 relationship without a step sequencer. Phase separates the voices around
the cycle. When two rhythms meet, a bounded collision system can exchange
energy, turn one overtone, or schedule a short flam. Hold freezes those
generative changes while the rhythm continues.

The patch is entirely self-exciting and requires no audio input. Open
`spectrum-tribe-gills.axp` in Ksoloti Patcher 1.0.12 and keep all five files in
this folder together.

## Gills controls

| Control | Function |
| --- | --- |
| Pot 1 | Low drum tuning |
| Pot 2 | Mid drum tuning |
| Pot 3 | High drum tuning |
| Pot 4 | Decay and resonator ringing |
| Pot 5 | Material: skin-like body through wood to bright metal |
| Pot 6 | Low drum hits per four-beat cycle, 1-9 |
| Pot 7 | Mid drum hits per four-beat cycle, 1-9 |
| Pot 8 | High drum hits per four-beat cycle, 1-9 |
| Pot 9 | Phase separation between the three rhythms |
| Pot 10 | Shared stereo spectral bloom |
| Button 1 | Cycle `EVEN`, `SWAY`, `CLAVE`, and `DRIFT` groove families |
| Button 2 | Mutate phase offsets and overtone relationships |
| Button 3 | Hold or release collision and drift mutations |
| Button 4 | Cycle `HARMONIC`, `PENTATONIC`, `JUST`, and `TRITAVE` tuning families |
| Encoder | Tempo, 40-180 BPM |
| Encoder push | Fill: strike all three voices |

LEDs 1-3 show low, mid, and high drum activity. LED 4 indicates Hold.

The display shows tempo, groove, tuning, movement state, the three hit counts,
phase, decay, material, space, and the collision counter.

## Starting point

Try low, mid, and high tuning around 25%, 45%, and 65%. Set Decay near 60%,
Material near 35%, and Space near 20%. Put the rhythm controls at approximately
25%, 38%, and 50% for a 3:4:5 texture, then raise Phase until the voices stop
landing together on every downbeat.

Material changes several related properties as one playable macro. At the low
end, the exciter is darker and the paired overtone stays close to the drum
body. Toward the high end, the attack brightens, the overtone moves upward, and
the three pairs exchange more sympathetic energy. Decay changes both ringing
time and excitation strength.

`EVEN` keeps the pulse field steady. `SWAY` stretches the two halves of the
cycle and alternates accents. `CLAVE` distributes a stronger three-accent
shape across each voice. `DRIFT` makes small bounded phase movements once per
cycle. Mutate creates a new nearby relationship; Hold keeps the current one.

## Spectral bloom

Space sends the dry mix into one shared four-line stereo feedback network. It
is intentionally compact and slightly resonant rather than a large neutral
reverb. The low end remains comparatively direct while high overtones spread
into the tail. Large Decay, Material, and Space settings can produce dense,
metallic clouds and engage the final limiter.

## Design and validation boundary

Spectrum Tribe is a separate sibling and does not replace or modify Spectral
Garden. Its six resonators, rhythm scheduler, collision rules, excitation, and
bloom are a new Ksoloti-native implementation. The scale-quantized resonator
idea is conceptually inspired by instruments including the 4ms Spectral
Multiband Resonator, but no 4ms source code, hardware design, branding, or
scale tables are included.

`spectrumtribe_dsp.h` uses the Mutable Instruments `stmlib` semitone-ratio
helper already distributed with Ksoloti under its MIT license. Offline builds
and host tests do not establish real-time CPU headroom, analog noise behavior,
output level, or musical balance. Test the patch with **Live** before choosing
any startup installation.

## Validation

XML validation and host C++ syntax compilation passed. A temporary behavioral
harness confirmed exact 3:4:5 primary event counts over four completed cycles,
collision-generated flams, debounced control buttons, bounded output, and a
50,000-block randomized control-and-fill stress run. The musical 3:4:5 case
measured -24.11 dBFS RMS and -2.37 dBFS peak; the stress case measured -12.43
dBFS RMS and -0.97 dBFS peak with 0.45% limiter engagement. The temporary
harness is not part of the project.

Ksoloti Patcher 1.0.12 completed the Cortex-M4 compile and link. Its target
report was 18,144 bytes of text, 36 bytes of data, 27,184 bytes of BSS, and
45,364 bytes total. No board was connected during that build, so hardware CPU
headroom, deadline safety, output level, and audible behavior remain unverified.
