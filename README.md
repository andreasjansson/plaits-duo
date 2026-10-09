# Plaits duo

A firmware for [Mutable Instruments Plaits](https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/),
based on the original firmware v1.1, which adds a **duo mode**: two voices of
the same model, the second one pitched by the LEVEL input, either mixed in
stereo on OUT and AUX, or split (one voice on each output). Everything else
is the original firmware, unchanged.

This is an independent project, not affiliated with or endorsed by Mutable
Instruments.

## Duo mode

- **Press the two buttons together** to enter or leave duo mode. Two LEDs move
  from the center to the edges when entering, and back when leaving. In duo
  mode, the LED of the model goes off twice, briefly, every 2 seconds.
- **Models**: the left button cycles through the 8 duo models in the stereo
  layout (green LED), the right button through the same models in the split
  layout (red LED). The original firmware and duo mode each remember their
  own selection, which is kept when the module is powered off. The MODEL CV
  is added to the selected model, with the same volts per model as in the
  original firmware.
- **LEVEL** is the pitch of the second voice, at 1V/octave (0V: the same pitch
  as the first voice). With nothing patched into LEVEL, there is only one
  voice. TRIGGER pings the low-pass gate of both voices (as in the original
  firmware with nothing patched into LEVEL). As in the original firmware,
  holding the left button shows the low-pass gate settings, and holding the
  right one the octave range.
- **Calibration of LEVEL**: in duo mode, hold the two buttons for 2 seconds
  (the bottom LED lights up green), patch 1V into LEVEL and press the left
  button (the LED turns yellow), then patch 3V and press the left button
  again. A calibration with wrong voltages is rejected (all the LEDs light up
  red). In the original firmware, the two buttons still calibrate V/OCT.

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

The firmware has 528 bytes of flash memory left (of 224kB). The RAM leaves
4.9kB for the stack, of which the benchmarks use at most 1.9kB.

CPU use, measured on an emulated Cortex-M4 with the timing of the module's
processor (`make bench`, see `plaits/test/bench.mk`): the most expensive
block of 12 samples of each duo model, with one and two voices, in % of the
budget (the most expensive block of the original firmware v1.1, built from
its own source: the modal resonator, 15268 cycles):

| model | 1 voice, stereo | 1 voice, split | 2 voices, stereo | 2 voices, split |
| --- | --- | --- | --- | --- |
| classic waveforms | 50% | 47% | 87% | 71% |
| waveshaper | 46% | 56% | 79% | 90% |
| 2-operator FM | 59% | 48% | 89% | 73% |
| formant | 51% | 44% | 78% | 66% |
| harmonic | 58% | 61% | 90% | 88% |
| wavetable | 66% | 56% | 92% | 78% |
| string | 96% | 89% | 96% | 89% |
| vowel | 67% | 63% | 87% | 90% |

To make room for duo mode in the flash memory, the code is built with less
loop unrolling (see `plaits/makefile`). The models of the original firmware
produce exactly the same output, but some of them use a little more CPU than
in v1.1: the most expensive one, the modal resonator, 0.6% more (15363
cycles); the others remain well below it (up to 4.5% more than in v1.1, for
the vowel and speech model).

## Changes to the original firmware

The duo mode code is in `plaits/dsp/duo/` and `plaits/dsp/duo_voice.cc`. The
original code is changed where duo mode needs it, without changing its
output: the user interface and the settings (duo mode, LEVEL calibration),
the voice (shared with duo mode), and the classic waveforms, waveshaper, FM
and formant engines, which duo mode reuses (they can skip AUX, or render in
stereo). `git diff upstream-v1.1` shows all the changes.

## License

MIT, as the original firmware: see [LICENSE](LICENSE).
