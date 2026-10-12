// Copyright 2026 Andreas Jansson.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Telemetry of the rig firmware (make -f plaits/makefile TARGET=plaits_rig,
// see docs/device-debugging-workflow.md): about every 2 seconds (or as set by
// a command, see rig/rig.h), AUX carries a frame of measurements instead of
// the voice, for 0.27s, which rig/host/telemetry.py decodes from a recording
// of AUX.
//
// A frame is a preamble of 32 zero bits, then the 32-bit words of a
// TelemetryFrame, most significant bit first. Each bit lasts 6 samples, as a
// Manchester code: a 1 is high for 3 samples, then low for 3; a 0 the
// opposite.

#ifndef PLAITS_RIG_TELEMETRY_H_
#define PLAITS_RIG_TELEMETRY_H_

#include "stmlib/stmlib.h"

#include "plaits/dsp/voice.h"
#include "plaits/rig/rig.h"
#include "plaits/settings.h"
#include "plaits/ui.h"

namespace plaits {

const uint32_t kTelemetryMagic = 0x504c5247;  // PLRG
const uint32_t kTelemetryVersion = 4;

// rig/host/telemetry.py reads the same fields, in the same order.
struct TelemetryFrame {
  uint32_t magic;
  uint32_t version_size;  // Version << 16 | number of words.
  uint32_t frame;

  // Since the previous frame: the number of blocks, the most cycles of the
  // processor used by a block (all of it, rendering the voice, and the UI and
  // CV processing), and the number of blocks which took longer than their
  // duration.
  uint32_t blocks;
  uint32_t cycles;
  uint32_t render_cycles;
  uint32_t poll_cycles;
  uint32_t overruns;

  // The readings of V/OCT and LEVEL (16-bit) summed since the previous
  // frame, and their squares (64-bit: the low word, then the high word).
  int32_t cv_sum[2];
  uint32_t cv_sum_squares[2][2];

  // The latest readings of all the CV inputs and pots.
  int16_t cv[CV_ADC_CHANNEL_LAST];
  uint16_t pots[POTS_ADC_CHANNEL_LAST + 1];

  // Semitones per unit of LEVEL CV until LEVEL is calibrated (see
  // Ui::UpdateLevelNoteScale).
  float level_note_scale;

  // Calibration data: V/OCT and LEVEL (scale, offset), and the calibration of
  // LEVEL as a pitch input.
  float v_oct_calibration[2];
  float level_calibration[2];
  float level_note_calibration[2];

  // The state of the module: patch.engine, patch.duo_mode, patch.duo_engine,
  // the active engine, the mode of the UI, and the patched inputs (FM,
  // TIMBRE, MORPH, TRIGGER, LEVEL: bits 0 to 4).
  uint8_t engine;
  uint8_t duo_mode;
  uint8_t duo_engine;
  uint8_t active_engine;
  uint8_t ui_mode;
  uint8_t patched;
  uint8_t calibrating;
  uint8_t padding;

  // The pitches of the two voices, in semitones (V/OCT, and LEVEL in duo
  // mode), as the voices receive them.
  float note;
  float level_note;

  // FNV-1a of the firmware's image in the flash memory (its .bin file), as
  // computed at startup (0 on the simulator).
  uint32_t image;

  // The commands received on FM (see rig/rig.h): executed, rejected, the
  // last one, and the function of the debug instrument.
  uint32_t commands;
  uint16_t command_errors;
  uint8_t last_command;
  uint8_t instrument;

  // Since the previous frame, the most cycles of each section of the duo
  // voice (RigSection), and since the statistics were cleared, the most
  // cycles of a block and the number of overruns of each duo_engine, as in
  // the release firmware (see Rig::EndBlock).
  uint32_t section_cycles[RIG_SECTION_LAST];
  uint32_t engine_cycles[kRigNumEngines];
  uint16_t engine_overruns[kRigNumEngines];

  // Since the previous frame, the most cycles of each probe (Rig::Probe),
  // their sum, and the number of times it was reached.
  uint32_t probe_max[kRigNumProbes];
  uint32_t probe_sum[kRigNumProbes];
  uint32_t probe_count[kRigNumProbes];

  uint32_t checksum;  // FNV-1a of the 32-bit words before it.
};

class Telemetry {
 public:
  Telemetry() { }
  ~Telemetry() { }

  void Init();

  // Once per block, after rendering it: start is the cycle counter at the
  // start of the block, poll after the UI and CV processing, render after
  // rendering the voice.
  void Process(
      const Ui& ui,
      const Patch& patch,
      const Modulations& modulations,
      const Settings& settings,
      uint32_t start,
      uint32_t poll,
      uint32_t render,
      int16_t* aux,
      size_t stride,
      size_t size);

 private:
  void Snapshot(
      const Ui& ui,
      const Patch& patch,
      const Modulations& modulations,
      const Settings& settings);

  uint32_t image_;
  TelemetryFrame next_;  // Being measured.
  TelemetryFrame frame_;  // Being sent.
  uint64_t cv_sum_squares_[2];
  int countdown_;  // Blocks until the next frame.
  int position_;  // Sample of the frame being sent, -1: none.

  DISALLOW_COPY_AND_ASSIGN(Telemetry);
};

}  // namespace plaits

#endif  // PLAITS_RIG_TELEMETRY_H_
