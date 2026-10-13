# Plaits duo

A firmware for [Mutable Instruments Plaits](https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/),
based on the original firmware v1.1, which adds a **duo mode**: two voices of
the same model, the second one pitched by the LEVEL input, either mixed in
stereo on OUT and AUX, or split (one voice on each output). Everything else
is the original firmware, unchanged.

This is an independent project, not affiliated with or endorsed by Mutable
Instruments.

**Try it in a browser**: [andreasjansson.github.io/plaits-duo](https://andreasjansson.github.io/plaits-duo/)
runs the firmware itself (compiled to WebAssembly, with a simulation of the
module's hardware), with a sequencer playing into its jacks, and offers the
firmware update file.

## Duo mode

- **Press the two buttons together** to enter or leave duo mode. Two LEDs move
  from the center to the edges when entering, and back when leaving.
- **Models**: the left button cycles through the 8 duo models in the stereo
  layout (yellow LED, where the original firmware is green), the right button
  through the same models in the split layout (red LED). In the colour-blind
  mode (the right button held at power on), the stereo layout is a dim yellow,
  between the full yellow of the original firmware's green and the faint
  yellow of its red. The original firmware and duo mode each remember their
  own selection, which is kept when the module is powered off. The MODEL CV
  is added to the selected model, with the same volts per model as in the
  original firmware.
- **LEVEL** is the pitch of the second voice, at 1V/octave (0V: the same pitch
  as the first voice). LEVEL was made for 0 to 8V: it reads down to about
  -0.6 to -0.9V (the converter reads 2.4 to 3.1% high, so it reaches its full
  scale a little early; this varies between modules), so the second voice
  goes 7 to 10 semitones below the pitch set by FREQUENCY at most. With nothing patched
  into LEVEL, there is only one voice. TRIGGER pings the low-pass gate of both
  voices (as in the original firmware with nothing patched into LEVEL). As in
  the original firmware, holding the left button shows the low-pass gate
  settings, and holding the right one the octave range.
- **Until it is calibrated**, LEVEL reads volts as V/OCT does: the two inputs
  have the same input stage, and the factory calibration of V/OCT accounts
  for what they share, such as the gain of the converter (which reads 2.4 to
  3.1% high, about 30 cents per octave). What it cannot account for are the
  tolerances of the resistors of the two input stages (1% parts): typically a
  few cents per octave, up to about 25.
- **Calibration of LEVEL** (recommended): in duo
  mode, hold the two buttons for 2 seconds. The bottom LED pulses green, and
  OUT and AUX carry two voltages in turn, about 0.5V and 4.4V, 15 times a
  second (a loud buzz: turn the mixer down). Patch OUT into V/OCT. When the
  bottom LED **flashes green** (about a second later: V/OCT has read the two
  voltages, and its readings have settled), press the left button: the LED
  pulses yellow. Move the cable from V/OCT to LEVEL, and when the LED
  flashes green again, press the left button: the LEDs show the model
  again, and the calibration is saved. With a stackable cable or a mult into
  both V/OCT and LEVEL, the first press is enough. LEVEL then reads exactly
  what V/OCT reads, whatever the calibration of V/OCT, so that the two
  voices stay in tune with each other across octaves. A press before the
  green flash, or with LEVEL not reading the same voltages as V/OCT (the
  cable in the wrong place), is rejected: all the LEDs light up red until a
  button is pressed, and the previous calibration is kept.
- **In the original firmware**, the two buttons still calibrate V/OCT, with
  1V and then 3V. Only V/OCT should be patched: the inputs detected as
  patched keep the offsets they had (the original firmware measured them
  again from whatever was patched), and voltages which are not about 2V
  apart are rejected (the original firmware accepted 1 to 3V apart, which
  saved a wrong calibration when the voltage was not the one expected).

The models (green 1 to 6 and 8 of the original firmware, with the
inharmonic string in place of the chords), and how each one makes its
stereo image:

| LED | model | stereo |
| --- | --- | --- |
| 1 | classic waveforms | one saw per channel, slightly detuned (the detuning drifts slowly), and the same square in both |
| 2 | waveshaper | the wavefolder is offset in opposite directions in the two channels (by an amount which breathes slowly) |
| 3 | 2-operator FM | moving diffuser: two different chains of allpass filters, whose delays move slowly (the bass stays in the middle); a few dB quieter than in the split layout, since the diffuser makes its peaks up to twice as high |
| 4 | formant | moving diffuser |
| 5 | harmonic | each harmonic has its own, slowly changing, phase difference between the channels (neighbouring harmonics turn in opposite directions; the fundamental is the same in both) |
| 6 | wavetable | the chorus of the Juno-60 (mode I), followed by a limiter |
| 7 | string | the ensemble of the string machine of Plaits 1.2, with mirrored taps in the two channels |
| 8 | vowel | moving diffuser |

In every model, the two channels have the same level in every frequency
band, and the image is centered on average. Nothing reaches full scale: the
models whose peaks grow with their stereo processing (FM, wavetable), and
the string and vowel models, go through a limiter, which turns the sound
down as soon as a peak would go above 0.8 (it rarely has to, except with
the string model, as in the original firmware). Some images move: with the
classic waveforms, the detuned saws make the image sway slowly from side to
side, and the harmonic model does the same when one or two harmonics
dominate the spectrum.

In the split layout, each voice is the OUT signal of the corresponding
model of the original firmware: the voice pitched by V/OCT on OUT, the one
pitched by LEVEL on AUX (with one voice, the same signal on both).

## Building and updating the module

```sh
git clone --recursive https://github.com/andreasjansson/plaits-duo.git
cd plaits-duo
make firmware TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain-13.3.rel1/
make wav TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain-13.3.rel1/
```

`TOOLCHAIN_PATH` is the directory containing `bin/arm-none-eabi-gcc` (the
[Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
13.3.rel1 was used). `make firmware` writes `build/plaits/plaits.bin` and
`plaits.hex`, for a programmer. `make wav` writes `build/plaits/plaits.wav`,
the firmware update file: hold the left button while powering the module on,
and play the file into the MODEL CV input (the upper LEDs show the signal
level, the lower ones the progress). It needs Python 3 with numpy
(`PYTHON=...` to use another Python than `python3`): `tools/qpsk_encoder.py`
is a Python 3 version of the encoder of `stm_audio_bootloader`, which only
runs on Python 2. The tests check that the bootloader decodes the update file
into the firmware.

The settings are compatible with the original firmware: after an update,
the model and the calibrations are kept, and going back to the original
firmware is possible at any time.

## Tests

```sh
make test
```

builds the test tools with the host compiler, including a simulator of the
module, and runs the tests ([cctr](https://github.com/andreasjansson/cctr)
0.31 or later). See [test/README.md](test/README.md): the original firmware
produces exactly the same output as v1.1; duo mode's buttons, LEDs,
calibration and settings; the split layout matches the original engines;
the stereo image of each model is balanced and centered. With
`TOOLCHAIN_PATH` and a Python with the `unicorn`, `capstone`, `pyelftools`
and `numpy` modules (`PYTHON`), the tests also build the firmware and check
its memory and CPU use, and its update file.

## Resources

The firmware has 2832 bytes of flash memory left (of 224kB). The RAM leaves
4.9kB for the stack, of which the benchmarks use at most 2.2kB.

CPU use measured on the module (the rig firmware, see below), in % of the
duration of a block of 12 samples (18048 cycles): the most expensive block of
each model, with two voices playing a phrase pinged by TRIGGER, TIMBRE rising,
as the release firmware runs it:

| model | 2 voices, stereo | 2 voices, split |
| --- | --- | --- |
| classic waveforms | 92% | 79% |
| waveshaper | 95% | 76% |
| 2-operator FM | 97% | 81% |
| formant | 95% | 83% |
| harmonic | 95% | 82% |
| wavetable | 95% | 82% |
| string | 94% | 83% |
| vowel | 64% | 56% |

Two known exceptions: the vowel and speech model decodes a whole word bank at
once when HARMONICS moves to another one (as in v1.1), and the wavetable
model, in the stereo layout with TIMBRE, MORPH and HARMONICS all modulated at
once, can take slightly more than a block when its position moves to a new
cell of the wavetable.

CPU use measured on an emulated Cortex-M4 with the timing of the module's
flash memory (`make bench`, see `plaits/test/bench.mk`): the most expensive
block of 12 samples of each duo model, with one and two voices, in % of the
budget (the most expensive block of the original firmware v1.1, built from
its own source: the modal resonator, 15268 cycles). The module is slower than
the emulation, and not in proportion (see
[docs/device-debugging-workflow.md](docs/device-debugging-workflow.md)):

| model | 1 voice, stereo | 1 voice, split | 2 voices, stereo | 2 voices, split |
| --- | --- | --- | --- | --- |
| classic waveforms | 47% | 41% | 81% | 67% |
| waveshaper | 44% | 37% | 76% | 59% |
| 2-operator FM | 55% | 42% | 87% | 69% |
| formant | 50% | 40% | 76% | 64% |
| harmonic | 54% | 51% | 84% | 76% |
| wavetable | 74% | 62% | 95% | 80% |
| string | 78% | 66% | 80% | 68% |
| vowel | 64% | 56% | 83% | 86% |

To make room for duo mode in the flash memory, the code is built with less
loop unrolling (see `plaits/makefile`), and the drum models of the original
firmware are optimized for size. The models of the original firmware produce
exactly the same output; the most expensive one, the modal resonator, uses
0.2% less CPU than in v1.1 (15239 cycles), and the others remain below it
(the drums now use 69 to 90% of it).

## Web demo

`web/` is the page: `make -f web/web.mk WASI_SDK_PATH=... BINARYEN_PATH=...`
builds it in `build/web/` (wasi-sdk 34 and binaryen 133; the firmware's update
file must have been built first, with `make wav`). Every push to `main` builds
it and publishes it on GitHub Pages (`.github/workflows/web.yml`).

## Changes to the original firmware

The duo mode code is in `plaits/dsp/duo/` and `plaits/dsp/duo_voice.cc`. The
original code is changed where duo mode needs it, without changing its
output: the user interface and the settings (duo mode, LEVEL calibration),
the voice (shared with duo mode), and the classic waveforms, waveshaper, FM
and formant engines, which duo mode reuses (they can skip AUX, or render in
stereo). `git diff upstream-v1.1` shows all the changes.

The calibration code (of V/OCT and LEVEL) is in `plaits/ui_calibration.cc`,
optimized for size. To make room in the flash memory, the factory test mode
of the original firmware is left out: on a module whose settings were blank,
it took commands from Mutable Instruments' test jig over the serial port of
the programming header, and output a test signal.

## Measurements on the module

`make rig` builds a second firmware, `build/plaits_rig/plaits_rig.wav`: the
same, with telemetry on AUX (every 2 seconds, for 0.14 seconds: the readings
of the inputs, the calibrations, and the CPU use measured by the processor's
cycle counter). `rig/rig.py` drives the module's CV inputs with an Ornate
Criminal, records OUT and AUX, and measures LEVEL against V/OCT (the two
voices given the same voltage through a mult), the lowest voltage LEVEL
reads, the calibration of LEVEL, the CPU use of every model, and the sound
against the simulator. `rig/rig.py --virtual` runs the same against the
simulator of the rig firmware. See
[docs/device-debugging-workflow.md](docs/device-debugging-workflow.md).

## License

MIT, as the original firmware: see [LICENSE](LICENSE).
