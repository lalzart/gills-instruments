# Afterring for Ksoloti Gills

Afterring combines two independent rhythmic voices. The first is a smooth
four-stage wave with three selectable oscillator sources. The second is a
lower tuned pulse with its own clock, rhythm, and scale-aware movement. It uses
a direct amplitude envelope and a short pitch fall, so every secondary event
has a clear attack, body, and pitch.

The instrument runs continuously. Button 1 cycles the main oscillator:

- `RND` morphs between a sine and triangle for the softest sound.
- `FOLD` uses the Mutable Instruments Braids sine-fold transfer function, with
  Pot 7 controlling the amount of folding.
- `REED` restores the original feedback waveguide voice.

The feedback wave occupies the diffuse background. Secondary pulses stay
mostly dry and briefly make a small amount of space in the wave on each hit,
so the two rhythms remain distinct without changing their individual levels.

The secondary rhythm has no random event dropouts. Its generative behavior
comes from evolving pattern rotation, pitch movement, and its relationship to
the four-stage wave. Pot 10 controls only its level, leaving the main sequence
unchanged while the two layers are balanced.

Open `afterring-gills.axp` in Ksoloti Patcher. Keep this folder beside
`tide-pit-gills` because Afterring shares Tide Pit's feedback-body code without
modifying that project.

## Controls

| Control | Function |
| --- | --- |
| Pots 1-4 | Primary four-note wave |
| Pot 5 | Primary cycle rate |
| Pot 6 | Memory / mutation frequency |
| Pot 7 | Main timbre/fold/reed material and pulse tone |
| Pot 8 | Secondary rhythm relationship |
| Pot 9 | Secondary melodic movement |
| Pot 10 | Secondary layer level |
| Button 1 | Cycle RND, FOLD, and REED main oscillators |
| Button 2 | Mutate once |
| Button 3 | Lock / release automatic evolution |
| Button 4 | Hold / release secondary evolution |
| Encoder | Root note (C2-C5) |
| Encoder tap | Change scale |
| Encoder hold | Change secondary source: current, previous, alternating, next |

Pot 8 selects eight relationships ranging from one pulse per primary step to
syncopated, three-against-two, five-against-four, and burst patterns. Pot 9
moves from low unison reinforcement into wider scale-aware counter-melodies.

Start with Pot 7 near noon, Pot 8 near 10%, Pot 9 near 30%, and Pot 10 near
50%. Turn Pot 10 down to hear the wave alone, then raise it until the pulse
rhythm sits where you want it.

Test DSP load and output level on hardware before installing as a startup
patch.
