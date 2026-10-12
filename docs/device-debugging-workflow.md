# Device debugging workflow: Plaits Duo on the hardware rig

How we check the Plaits Duo firmware on a real module, and find what the simulator and the
emulated benchmarks miss. This doc is self-contained; the details live in:
- `rig/config.json`: the wiring (authoritative) and the virtual module;
- the docstrings of `rig/rig.py` and `rig/host/*.py`;
- `plaits/rig/telemetry.h`: what the rig firmware reports;
- `plaits/ui_calibration.cc`: how LEVEL is calibrated.

## Why it exists

Everything else is tested off the module: the firmware runs on the computer in `plaits-sim`
(the tests in `test/`), and its CPU use is measured on an emulated Cortex-M4
(`plaits/test/bench.mk`). What only the module can tell:
- **LEVEL as a pitch input.** Mutable calibrated V/OCT at the factory, never LEVEL. Out of
  the box, LEVEL reads volts as V/OCT does. Whether that's close depends on the tolerances of
  the two input stages' resistors, which only a measurement shows. Also: how low LEVEL reads
  (the op-amp's swing), and how noisy it is.
- **The calibration of LEVEL against V/OCT**, with the module's own voltages on OUT.
- **CPU use on the real processor.** The string model uses 96% of the budget on the
  emulator (the budget being the most expensive block of v1.1); an overrun would glitch
  the audio.
- **The sound against the simulator**: levels, stereo, spectra, with the same knobs and CVs.

## Hard rules (read first)

- **Flashing is the user's job.** Power the module on with the left button held, then play
  the WAV into **MODEL**, FREQUENCY at 12 o'clock, sounds and speakers muted. The upper LEDs
  show the level (2 or 3 lit), the lower ones the progress; the module restarts when done.
  All LEDs blinking red: press the left button and play it again, louder.
  - Recovery: the bootloader is never overwritten. Flash again, any firmware.
  - After a session, flash the release back (`build/plaits/plaits.wav`): the rig firmware
    interrupts AUX every 2 seconds.
- **Build firmware only with the makefiles** (`make wav`, `make rig`). The tests decode every
  update file with the bootloader's own code and compare it with the firmware.
- **Ask the user before anything outward-facing**: flashing anything, downloading software,
  publishing, changing the user's equipment. The oC's FX Rig app is fxaid's; Plaits needs
  nothing new on the oC.
- **Shared files:** delete only files you created, by name. `rig/results/` is not committed.

## The rig

| Part | Role |
| --- | --- |
| Mac | runs `rig/rig.py`: sets the CVs, records, decodes the telemetry, analyses |
| Ornate Criminal (oC), FX Rig app | its DAC outputs drive the module's CV inputs (over USB, with ocpilot) |
| Plaits, rig firmware | the module under test |
| Scarlett 8i6 | records OUT and AUX |

**Wiring** (`rig/config.json` is authoritative):
- **oC A → passive mult (or a stackable cable) → V/OCT and LEVEL.** The two inputs get exactly
  the same voltage, so the interval between the voices measures LEVEL against V/OCT, whose
  factory calibration is the reference. The oC's own accuracy doesn't matter.
- oC B → TRIGGER, only when an experiment asks (a patched TRIGGER closes the low-pass gate
  between pings).
- oC C → MODEL: re-patch it after flashing (MODEL is the update input).
- oC D → HARMONICS, E → TIMBRE, F → MORPH.
- Plaits **OUT → Scarlett in 1, AUX → in 2** (line, gain at minimum). In the split layout,
  voice 1 is on OUT and voice 2 on AUX.
- **Knobs**: FREQUENCY at about 9 o'clock, HARMONICS at 12, TIMBRE fully counterclockwise,
  MORPH at about 11 (a clean pitch for the classic waveforms). TIMBRE and MORPH attenuverters
  fully clockwise, FM attenuverter centered.
- **Module state**: duo mode, split layout, model 1 (classic waveforms). `check` says if not.

## Firmware types

| Firmware | Build | Use |
| --- | --- | --- |
| release | `make wav` → `build/plaits/plaits.wav` | what users flash |
| rig | `make rig` → `build/plaits_rig/plaits_rig.wav` (and `plaits_rig_install.wav`) | the release in duo mode only (208 kB), plus telemetry on AUX, commands on FM, a debug instrument, timing probes, and an updater |

Both need `TOOLCHAIN_PATH` (Arm GNU Toolchain 13.3.rel1) and a `PYTHON` with numpy. The rig
firmware has the release's DSP code and compiler flags, so its models' CPU use is the
release's; it reports the statistics of each model as in the release (without the blocks in
which it executed a command, nor its command decoder: about 400 cycles a block).

**Remote control** (`plaits/rig/rig.h`, `rig/host/control.py`): the rig sends commands on FM
from oC jack H (role debug: levels of 5 ms, self-calibrating, with a check byte), so it works
without anyone at the module: the buttons, the model and layout, the knobs, the CV readings,
the patched inputs, the telemetry (its interval, a frame on request, the statistics cleared or
frozen), the debug instrument (a sine, constant samples, the readings of two inputs, or the
voice with cycles burnt after it), a restart, and the updater.

**Updates without the bootloader** (`plaits/rig/updater/`): the rig firmware's updater, in the
last 16 kB of the flash, receives an update on MODEL from the audio interface's software
output 2 at -3 dBFS: `rig.py flash` (about 2.5 minutes), which checks the new image's hash in
the telemetry. Only a broken updater needs the user: power on with the left button held (the
bootloader), then `rig.py install` (the rig firmware and its updater). The release firmware is
too large for the updater: flash it with the bootloader.

**Telemetry** (`plaits/rig/telemetry.h`, version 4): every 8000 blocks (2.005 s), or on
request, AUX carries a frame instead of the voice, for 0.33 s. It is 32 zero bits of preamble,
then the 89 words of a `TelemetryFrame` (the last one an FNV-1a checksum), Manchester coded, 6
samples per bit. `rig/host/telemetry.py` decodes it from a recording, either polarity, any
clock within ±300 ppm, AC-coupled or low-passed. It also measures the module's sample rate
from the spacing of the frames. A frame holds:
- the sums of V/OCT's and LEVEL's 16-bit readings since the previous frame, and of their
  squares, as integers (exact means and noise);
- the latest readings of all inputs and pots;
- the calibrations (V/OCT, LEVEL's 0V reading, LEVEL as a pitch input);
- the module's state: mode, model, layout, active model, UI mode, patched inputs;
- the notes the two voices get;
- the most cycles per block since the previous frame (all of it, the voice, the UI), and
  the blocks longer than their 18048 cycles (overruns);
- the image's hash, the commands received, the most cycles of the sections of the voice
  (control, engine, stereo effects, output), and of each model with its overruns;
- the probes: `RIG_PROBE(i)` (`plaits/rig/probe.h`) anywhere in the code times the part since
  the previous probe (most, sum and count). They compile to nothing in the release.

## A session

The rig's Python needs numpy, scipy, sounddevice and ocpilot: fxaid's environment has them.

```bash
make rig TOOLCHAIN_PATH=... PYTHON=...                  # rig firmware WAV + its simulator
PY=~/projects/fxaid/.venv/bin/python
$PY rig/rig.py --virtual session                        # dry run against plaits-sim-rig, ~25s
$PY rig/rig.py devices                                  # the Scarlett and the oC present?
$PY rig/rig.py check                                    # module state and wiring, from the telemetry
$PY rig/rig.py session                                  # everything below, guided (~15 min)
$PY rig/rig.py levelcal floor                           # some experiments, in order
$PY rig/rig.py decode rig/results/.../003-levelcal-+1.00V.npz   # the frames of a capture
$PY rig/rig.py flash                                    # the rig firmware just built, remotely
$PY rig/rig.py frame                                    # the module's state now
$PY rig/rig.py ctl engine 6 split                       # a command (control.Control's methods)
$PY rig/rig.py tour                                     # the listening tour (and --virtual tour)
$PY rig/host/compare.py rig/results/tour-... rig/results/virtual-tour-...
$PY rig/rig.py loops                                    # the web demo's loops, recorded
```

Each run writes `rig/results/<experiments>-<date>/`: one JSON file per experiment, every
capture as `.npz` (OUT, AUX, sample rate), and `log.txt`. When the module needs a hand (a
cable moved, a button pressed), the rig asks and waits for Enter.

| Experiment | Measures | Expect (simulator, typical module) |
| --- | --- | --- |
| `check` | state, wiring: 0 V → 2 V moves both readings by -0.4 and both voices by 24 semitones; MODEL CV selects model 2 | no PROBLEM lines |
| `levelcal` | from -0.5 to 6 V: V/OCT's and LEVEL's readings, both voices' pitch; each way of reading LEVEL against V/OCT, in cents at 0 V and per octave | earlier firmware ~+30 cents/octave (the converter reads ~2.7% high); out of the box a few cents/octave; calibrated < 1 |
| `floor` | the lowest voltage LEVEL follows | -0.6 to -0.9 V (the converter's gain error makes it reach full scale early: -0.70 V on the virtual module) |
| `noise` | the readings' noise (telemetry) and the voices' pitch jitter (audio) | a few cents rms before the firmware's smoothing |
| `selfcal` | the calibration against V/OCT, guided | a scale near -58 semitones per unit |
| `levelcal-after-selfcal` | the same as `levelcal`, calibrated | under 1 cent per octave |
| `cpu` | the most cycles per block of every model, both layouts, 2 voices, HARMONICS/TIMBRE/MORPH swept, without and with triggers (8 Hz) | the string model the most; no overruns |
| `compare` | every model, both layouts, one voice, pinged at 4 Hz, against `plaits-sim` with the same knobs (from the telemetry) and CVs: levels, L/R correlation, spectra | levels within a fraction of a dB; correlation close (the stereo effects drift) |
| `loops` | the loops of the web demo (`rig/host/loops.py`: their CVs computed by the page's own sequencer, `rig/host/web_loops.js`), each on its model in the stereo layout, with spoken titles; `loops.m4a` | no overruns |
| `tour` | a listening tour (`rig/host/tour.py`): every model, both layouts, playing a phrase (two voices a third apart, pinged), with spoken announcements; `tour.m4a`. Needs LEVEL on oC B and TRIGGER on oC G instead of the mult. `python rig/host/tour.py <results folder>` rebuilds it from the captures | the clips whose frames don't decode overrun (the announcement says so) |

**How the levels are compared.** On the virtual rig the module is the simulator, so `compare`
checks the pipeline: levels match within about 0.5 dB. On the module, the levels add the
analog path (DAC, output stage, Scarlett), so look for differences between models, not for an
overall offset.

## The debugging loop

1. **Symptom on the module**, reported or shown by a session.
2. **Reproduce it in the simulator**, under the module's conditions:
   - `rig.py --virtual --hw "..."` with plaits-sim commands for the hardware's errors:
     `gain_error JACK X` (an input stage's gain), `hw adc_gain X` (the converter's gain:
     the virtual module defaults to 1.027, typical), `hw vdda X` (the 3.3 V reference),
     `hw swing X` (the op-amps' distance to their rails);
   - the virtual module is calibrated as at the factory: the original firmware's V/OCT
     calibration with exact 1 V and 3 V, which also measures every input's 0 V reading;
   - plaits-sim scripts (`plaits/test/plaits_sim.cc`): `patch JACK main|aux` patches an output
     into an input.
3. **If the simulator can't reproduce it, measure more**: probes (`RIG_PROBE`), or a field of
   the telemetry frame; `make rig`, `rig.py flash`, run the experiment.
4. **Fix it, with a test** in `test/` (cctr). `make test TOOLCHAIN_PATH=... PYTHON=...` also
   builds both firmwares, checks their flash and the CPU benchmark, and decodes the
   telemetry of the rig simulator.
5. **Check on the module**: `make rig`, `rig.py flash`, the session (or the experiments
   concerned), then flash the release back (with the bootloader).

**CPU on the module.** The emulated benchmark (`make bench`) counts instructions and the
flash's wait states, but the module is slower, and not in proportion: each read of a constant
from the flash costs about 5 cycles (it also stalls the fetching of the instructions), a
taken branch inside a loop more than it seems, and code run from the RAM is no faster.
Measure on the module (`cpu`, or the probes) before keeping an optimization. What works:
constants kept in registers through a loop (`InRegister`, `plaits/dsp/dsp.h`), the state of a
loop in local variables (members are written back at every sample when the output could
alias them), fewer instructions per sample (they are fetched 8 bytes per 3 cycles), no
`std::copy` or struct copies in the audio code (newlib-nano's memmove and memcpy go a byte at
a time). Each optimization is checked against the output: the benchmark hashes the output of
every model (`build/bench/duo.json`), and the simulator's renders compare bit for bit.

## Gotchas

- **MODEL is the update input** (the audio interface's output 2): the MODEL CV from the oC
  is unused; the rig selects models by command.
- **A model which overruns garbles the frames**: the rig reads them with a light model
  selected (`frame(light=...)`); the statistics of each model are kept until cleared.
- **TRIGGER**: patched, it closes the low-pass gate between pings, so the pitch experiments
  need it unplugged. `cpu` and `compare` ask for it, then ask to unplug it.
- **The first frame after power-on** mixes the boot (unpatched jacks, settling readings).
  Every experiment waits 2.3 s after a change, so the frame it uses is all after it.
- **Telemetry interrupts AUX.** The analyses skip the frames, including those cut off at the
  ends of a capture: they're predicted from the decoded ones.
- **Scarlett outputs 1–2** follow the monitor knob, and the rig doesn't use them.
- **Two voices in unison** add up differently on each run, depending on their phases. That's
  why `compare` runs with one voice.
- **The string and speech models** sound different depending on how they were last excited,
  so `compare` pings them.

## File map

```
rig/rig.py                 CLI: devices, decode, the experiments, session; --virtual, --hw
rig/config.json            audio device and inputs, oC roles, jacks fed by each role, virtual module
rig/host/telemetry.py      the frame's layout, the decoder, masks of AUX without telemetry
rig/host/pitch.py          frequency of a voice (coarse period, then the fundamental's phase)
rig/host/rigio.py          the hardware rig (sounddevice + oC) and the virtual one (plaits-sim-rig)
rig/host/oc.py             the oC's FX Rig app over ocpilot (fxaid's rig/oc/FxRig.h)
rig/host/experiments.py    the experiments, and SESSION
rig/host/control.py        the commands of the rig firmware, flash and install
rig/host/tour.py           the listening tour; compare.py: against the virtual tour
rig/host/loops.py          the web demo's loops on the module (web_loops.js: their CVs)
rig/results/               one folder per run (not committed)
plaits/rig/telemetry.*     the telemetry of the rig firmware (make -f plaits/makefile TARGET=plaits_rig)
plaits/rig/rig.*           its commands, debug instrument, measurements; probe.h: RIG_PROBE
plaits/rig/updater/        its updater (from the audio bootloader)
plaits/test/plaits_sim.cc  the simulator (plaits-sim, and plaits-sim-rig with the telemetry)
plaits/test/sim/           the simulated hardware (input stages, converter, probe, outputs into inputs)
test/rig/rig.txt           the rig firmware fits; the telemetry decodes
```

## Open items (2026-10-12)

- The vowel and speech model decodes a whole word bank in one block when HARMONICS moves to
  another bank (as in v1.1): about 26 blocks of audio are lost then.
- The wavetable model, in the stereo layout, with TIMBRE, MORPH and HARMONICS all sweeping:
  up to 103% of a block when the position enters a new cell (its waves are read from the
  flash until they are copied); prefetching the next cell's waves would fix it.
