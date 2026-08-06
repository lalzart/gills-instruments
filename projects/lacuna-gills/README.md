# Lacuna for Ksoloti Gills

Lacuna is an inverted rhythm instrument. Its three-layer sound field runs
continuously, while button presses remove BODY, FORM, AIR, or the complete
output. The sound engine keeps advancing behind every absence, so its return
reveals a later state rather than restarting a note.

Tap the encoder switch to begin a new four-beat capture immediately. Play the
four gap buttons during that phrase; Lacuna then loops those absences. Holding
the encoder switch for about 0.6 seconds clears the captured pattern. A new tap
replaces the previous pattern rather than entering an overdub mode.

Open `lacuna-gills.axp` in Ksoloti Patcher 1.0.12 and keep all five files in this
folder together.

## Gills controls

| Control | Function |
| --- | --- |
| Pot 1 | Fundamental pitch |
| Pot 2 | FORM harmonic spread |
| Pot 3 | Spectral density |
| Pot 4 | AIR noise color |
| Pot 5 | Internal stereo movement |
| Pot 6 | Minimum duration of a tapped gap |
| Pot 7 | Edge shape from knife-cut to slow breath |
| Pot 8 | BODY, FORM, and AIR gap depth |
| Pot 9 | Bounded extra gaps derived from the captured pattern |
| Pot 10 | Re-entry scar amount and persistence |
| Button 1 | Remove BODY; hold to extend |
| Button 2 | Remove FORM; hold to extend |
| Button 3 | Remove AIR; hold to extend |
| Button 4 | Remove the complete output; hold to extend |
| Encoder | Tempo, 40-180 BPM |
| Encoder tap | Replace and record one four-beat gap performance |
| Encoder hold | Clear the captured gap pattern |

LEDs 1-3 are normally illuminated and go dark when their corresponding layer
is absent. LED 4 goes dark during VOID. The display compresses the 64-step
captured phrase into sixteen characters: `b`, `f`, `a`, `V`, or `*` for a
combination.

## Negative rhythm behavior

Pot 7 always retains a short smoothing ramp, even at the knife end, to keep
button edges controlled. Pot 8 can leave a spectral shadow for the three layer
buttons, but VOID always targets zero regardless of depth. Pot 10 adds energy
only after a layer returns; it never fills the middle of an absence.

Mutation leaves the stored performance unchanged. Fully left is exact loop
playback. Higher settings copy a small number of existing gaps to nearby
positions or extend them by one step. Only the highest range can occasionally
turn a derived layer gap into VOID.

## Design and validation boundary

The field oscillators, gap envelopes, fixed 64-step capture, bounded mutation,
and re-entry scars are a new Ksoloti-native implementation. The header uses the
`stmlib` semitone-ratio helper distributed with Ksoloti under its MIT license.

Offline builds and host tests cannot establish real-time CPU headroom, output
level, edge audibility, or the musical balance of BODY, FORM, AIR, and scars.
Test the patch with **Live** before selecting any startup installation.

## Validation

XML validation and host C++ syntax compilation passed. A temporary behavioral
harness confirmed continuous self-excitation, independent BODY removal,
debounced gap controls, exact zero output during sustained VOID, clean return,
four-beat capture, recorded-gap playback, exact unmutated playback at Pot 9
fully left, encoder-hold clearing, bounded output, and a 50,000-block randomized
control and gap stress run. The stress case measured -13.19 dBFS RMS and -0.97
dBFS peak with 0.001% final limiter engagement. The temporary harness is not
part of the project.

Ksoloti Patcher 1.0.12 completed Cortex-M4 code generation, compilation, and
linking on two validation passes. Its target report was 14,368 bytes of text,
4 bytes of data, 1,800 bytes of BSS, and 16,172 bytes total. The board was not
accessible for Live testing, so hardware CPU headroom and audible behavior
remain unverified.
