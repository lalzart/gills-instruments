# Gills Instruments

A working collection of custom embedded-DSP instruments for the Ksoloti Gills board. The projects combine Ksoloti patch graphs, local DSP headers, hardware-oriented validation, and per-instrument documentation/attribution.

Each instrument keeps its patch, local object, DSP headers, documentation, and
license together under `projects/`.

## Instruments

| Instrument | Character |
| --- | --- |
| Afterring | Four-stage feedback wave with an independent tuned pulse rhythm |
| Braidlink | Monophonic Digitakt-focused macro voice |
| Braidlink Poly4 | Four-voice version of Braidlink |
| Knob Loom | Ten modal threads tuned and excited by the ten pots |
| Lacuna | Continuous field performed by removing and looping sound gaps |
| Palimpsest | Four-stage modal voice with delayed scale-aware traces |
| Spectral Garden | Six-band self-exciting or input-driven resonator bank |
| Spectrum Tribe | Three-voice polyrhythmic resonator percussion instrument |
| Tide Pit | Grungier four-stage feedback and granular instrument |
| Tidepool | Four-stage physical-model and granular instrument |

Open an instrument's `.axp` file in Ksoloti Patcher. Keep the project folders
as siblings: Afterring intentionally includes
`../tide-pit-gills/tidepit_voice.h`.

## Dependencies

The projects use Ksoloti's firmware headers, Mutable Instruments sources, and
Gills patch objects. Those upstream sources are not vendored here. The default
development checkout is `/Users/lanceship/Projects/ksoloti`, and Ksoloti
Patcher 1.1.0 is preferred for complete Cortex-M4 compile/link checks.

## Validation

Run all portable XML and host syntax checks with:

```sh
scripts/validate-all.sh
```

To use another Ksoloti checkout:

```sh
KSOLOTI_REPO=/absolute/path/to/ksoloti scripts/validate-all.sh
```

A successful host check or offline Cortex-M4 link does not establish physical
board timing, CPU headroom, MIDI timing, analog noise, output level, or sound
quality. Test those on Gills before installing a startup patch.

Each instrument has its own license and third-party attribution notes.
