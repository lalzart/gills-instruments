# Gills Instruments Workspace Instructions

These instructions apply to the whole repository. Use the
`$build-ksoloti-gills-instruments` skill for instrument work.

## Scope and preservation

- Do not change an existing instrument unless the user explicitly asks for a
  change.
- Treat diagnosis, explanation, review, and brainstorming as read-only.
- Preserve existing defaults, control mappings, startup voice, and sound
  behavior unless the requested change requires otherwise.
- Create substantially different designs as sibling instruments under
  `projects/<name>-gills/`.
- Do not stage, commit, push, upload to the board or SD card, or flash firmware
  unless the user explicitly requests that action.
- Preserve unrelated and untracked work. Check `git status` before editing.

## Before editing

1. Read the target README, license, `.axp`, `.axo`, and included DSP headers
   completely.
2. Resolve local includes and sibling-project dependencies.
3. Identify the behavior that must remain unchanged.
4. Limit writes to the requested instrument unless the user approves a shared
   dependency change.

Afterring depends on `../tide-pit-gills/tidepit_voice.h`; keep those folders as
siblings unless intentionally making Afterring self-contained.

## Project structure

Keep the `.axp`, local `.axo`, DSP headers, README, and license in each project
folder. Prefer project-local objects and relative includes over changes to the
Ksoloti factory or community libraries. Keep Mutable Instruments code use and
conceptual inspiration distinct, retaining upstream attribution and notices.

## Real-time DSP constraints

- Target the STM32F427 audio deadline, not desktop-host performance.
- Use fixed-size storage and do not allocate in audio processing.
- Keep expensive math, random setup, coefficient calculation, and recipe
  selection out of per-sample and per-voice hot loops when possible.
- Prefer firmware lookup tables and existing `stmlib` or Mutable Instruments
  helpers over heavyweight dependencies.
- Debounce raw Gills buttons when one physical press must produce one action.

## Validation and handoff

Run checks in increasing order of cost:

1. `scripts/validate-all.sh` or the equivalent XML and host syntax checks.
2. Host behavioral, regression, and stress tests proportional to the change.
3. Target ARM compile/link of the complete patch, preferably with Ksoloti
   Patcher 1.1.0.
4. User hardware testing for CPU/deadline behavior, MIDI timing, noise, levels,
   and sound quality.

Use a sample-for-sample regression when preserving an original signal path is
important and practical. Report exactly which checks passed. Never reinterpret
a host sweep or successful ARM link as physical-board or audible validation.

At handoff, name the changed files, preserved behavior, validation results,
remaining hardware checks, and expected audible tradeoffs. Do not create a
commit unless requested.
