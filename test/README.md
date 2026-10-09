# Tests

The tests are [cctr](https://github.com/andreasjansson/cctr) corpus files
(cctr 0.31 or later): each test is a shell script and its expected output.

```sh
make test
```

builds the test tools with the host compiler and runs all the tests. The
firmware tests also need an ARM toolchain (`TOOLCHAIN_PATH`, the directory
containing `bin/arm-none-eabi-gcc`; the measurements below were made with the
Arm GNU Toolchain 13.3.rel1) and a Python with the `unicorn`, `capstone`,
`pyelftools` and `numpy` modules (`PYTHON`); they are skipped otherwise:

```sh
make test TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain-13.3.rel1/ PYTHON=/path/to/python3
```

The test tools (see `plaits/test/host.mk`):

- `plaits-sim`: this firmware (UI, settings and DSP code) running on the
  computer, with simulated knobs, jacks, buttons and LEDs, driven by a
  script read from stdin (see `plaits/test/plaits_sim.cc` for the commands).
  MAIN and AUX are written to a stereo WAV file. The settings are kept in
  the file named by `PLAITS_SIM_FLASH`, if it is set.
- `plaits-sim-v1.1`: the same simulator, built from the unmodified source of
  the original v1.1 firmware (the `upstream-v1.1` tag), as a reference.
- `plaits-analyze`: measurements on the WAV files (levels, pitch, stereo
  image, differences between recordings).
- `plaits-update-decoder`: decodes a firmware update file with the code of
  the audio bootloader.

`leds` prints the 8 LEDs from top to bottom: `G` (green), `R` (red), `Y`
(both colors) or `.` (off). The scripts start by waiting for the knob
readings to settle after power on.

## v1.1

The original firmware is unchanged.

- `ui.txt`: the buttons and LEDs behave as in the original firmware. Each
  script runs on this firmware and on the upstream v1.1 source: the outputs
  must be the same, and are shown.
- `models.txt`: all the models of the original firmware produce exactly the
  same output as the upstream v1.1 source. The workout script turns knobs,
  sends pitch, FM, TIMBRE, MORPH, trigger and LEVEL CVs. `max_diff` is the
  largest difference between the two recordings, in units of the least
  significant bit.

## duo

- `ui.txt`: pressing the two buttons together enters (or leaves) duo mode.
  In duo mode, the buttons select one of the 8 duo models, in the same way
  as in the original firmware: classic waveforms, waveshaper, 2-op FM,
  formant, harmonic, wavetable, string and vowel. The LED is green when the
  two voices are mixed in stereo on MAIN and AUX, red when they are split
  (the voice pitched by V/OCT on MAIN, the one pitched by LEVEL on AUX).
- `split.txt`: in the split layout, with nothing patched into LEVEL, MAIN
  and AUX both carry the voice pitched by V/OCT, which is the OUT signal of
  the corresponding model of the original firmware (string: red 4, the
  other models: green 1 to 6 and 8). Each script selects the model, then
  power cycles the module (the selected model is kept in flash) so that both
  recordings start in the same state, and runs the workout. The classic
  waveforms, waveshaper, FM and formant models use the code of the original
  engines and are bit-exact. The other models use faster versions of the
  original engines, which round differently (by a few units of the least
  significant bit).
- `level.txt`: LEVEL is the pitch of the second voice (1V/octave), and can be
  calibrated like V/OCT: hold the two buttons in duo mode (LED 7), patch 1V
  and press the left button, then 3V. A calibration with wrong voltages is
  rejected. In the original firmware, the two buttons still calibrate V/OCT.
- `stereo.txt`: the stereo image of each model, with one and two voices,
  over 20 seconds (see `plaits-analyze image`): the two channels have the
  same level in every octave band (within 3dB), the image is centered on
  average (lean within 0.25: +1 would be fully on the side of MAIN), wide
  (correlation below 0.9: 1 would be mono), and the bass stays in the middle
  (except for the classic waveforms, whose saws are detuned, and the
  harmonic model, whose fundamental is the same in both channels). No
  model reaches full scale (the models with a limiter stay below 0.8). In
  the split layout, the voices are not processed.
- `settings.txt`: the settings are compatible with the original firmware in
  both directions: after an update, the model and the calibrations are kept;
  after going back to the original firmware, it starts with the model it
  had, and the duo mode settings are still there when updating again.

## firmware

- `firmware.txt`: the firmware fits in the flash memory, with room for the
  stack in the RAM; on the module's processor (emulated, see
  `plaits/test/bench.mk`), every duo model uses less CPU than the most
  expensive model of the original firmware, which uses about as much as in
  v1.1 (within 1%); and the firmware update file is decoded by the code of
  the audio bootloader into the firmware.
