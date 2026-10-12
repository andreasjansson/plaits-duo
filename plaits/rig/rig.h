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
// The control of the rig firmware (make -f plaits/makefile TARGET=plaits_rig,
// see docs/device-debugging-workflow.md), by commands which the rig sends on
// the FM input (FM is then never seen as patched):
// - the buttons, pressed for a while or held;
// - the model (and layout), without saving it, nor the MODEL CV added;
// - the knobs and the CV inputs, at given values instead of what they read;
// - the detection of patched inputs, forced either way;
// - the telemetry (rig/telemetry.h): its interval, a frame now, and the
//   statistics it reports, cleared;
// - the debug instrument, which replaces the voice on OUT and AUX: a sine
//   (dropped or repeated blocks show as jumps of its phase), constant
//   samples, the readings of two CV inputs, or the voice with cycles burnt
//   after it (to measure the headroom of a model);
// - the updater (plaits/rig/updater), which receives the next firmware.
//
// A command is a series of voltages, each held for at least 1ms. FM rests at
// -2V; +4V starts the command; then each byte is 4 symbols of 2 bits, the
// most significant first, at -0.8V, 0.4V, 1.6V or 2.8V, each after -2V. The
// bytes are the command, the number of bytes of its arguments, the
// arguments, and a check byte (see Rig::Execute and rig/host/oc.py). The
// levels are read relative to the resting level and to the start (FM's gain
// and offset vary: about -0.125 per volt on the module).
//
// It also measures the cycles used by the sections of the duo voice, and the
// most cycles used by each model since the statistics were cleared, which
// the telemetry reports.

#ifndef PLAITS_RIG_RIG_H_
#define PLAITS_RIG_RIG_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "plaits/dsp/voice.h"

#ifndef TEST
#include <stm32f37x_conf.h>
#endif  // TEST

namespace plaits {

enum RigCommand {
  RIG_COMMAND_BUTTONS = 1,  // Mask (1: left, 2: right), duration (x4ms, 0: held).
  RIG_COMMAND_ENGINE,  // duo_engine (+16: the buttons and MODEL CV again).
  RIG_COMMAND_KNOB,  // Pot, value (16 bits, 0xffff: the knob again).
  RIG_COMMAND_CV,  // Channel, reading (16 bits), 1: override, 0: the input again.
  RIG_COMMAND_PATCHED,  // Mask of the forced inputs, their states.
  RIG_COMMAND_TELEMETRY,  // Interval (16 bits, blocks, 0: none), a frame in (x4ms, 0: none).
  RIG_COMMAND_RESET,  // Clears the statistics (1: freezes the sections' instead).
  RIG_COMMAND_INSTRUMENT,  // Function, 3 arguments (16 bits).
  RIG_COMMAND_UPDATE,  // 0x55, 0xaa: starts the updater; 0x5a, 0xa5: restarts.
};

enum RigInstrument {
  RIG_INSTRUMENT_OFF,
  RIG_INSTRUMENT_SINE,  // Frequency (Hz), amplitude of OUT, of AUX (90 deg. later).
  RIG_INSTRUMENT_CONSTANT,  // Sample on OUT, on AUX.
  RIG_INSTRUMENT_READINGS,  // CV channel on OUT, on AUX (readings, inverted).
  RIG_INSTRUMENT_BURN,  // The voice, then cycles burnt: up to this many in the block.
};

// Sections of the duo voice, timed by Rig::Mark.
enum RigSection {
  RIG_SECTION_CONTROL,  // Trigger, envelopes, parameters.
  RIG_SECTION_ENGINE,  // The engine's rendering.
  RIG_SECTION_STEREO,  // Stereo effects, limiter.
  RIG_SECTION_OUTPUT,  // LPG and conversion of OUT and AUX.
  RIG_SECTION_LAST
};

const int kRigNumEngines = 16;  // duo_engine: model, and 8 for the split layout.
const int kRigNumProbes = 8;
const uint32_t kRigBlockCycles = 18048;  // 12 samples at 47872Hz, at 72MHz.
const int kRigMaxArguments = 10;

class Rig {
 public:
  Rig() { }
  ~Rig() { }

  void Init(Patch* patch);

  // Once per block, with the readings of the CV inputs (in CvAdcChannel
  // order, FM third).
  void ProcessCommands(const int16_t* readings);

  // The overrides, applied by the drivers (see plaits/drivers/*_adc.h) and
  // the UI.
  inline bool pot_overridden(int pot) const {
    return pot_override_[pot] != 0xffff;
  }
  inline uint16_t pot_override(int pot) const { return pot_override_[pot]; }
  inline bool cv_overridden(int channel) const {
    return cv_override_mask_ & (1 << channel);
  }
  inline int16_t cv_override(int channel) const {
    return cv_override_[channel];
  }
  inline bool button_forced(int button) const {
    return (button_mask_ >> button) & 1;
  }
  // The patched inputs (FM, TIMBRE, MORPH, TRIGGER, LEVEL), as forced.
  void ApplyPatched(Modulations* modulations) const;

  // The debug instrument, on OUT and AUX, after the voice has rendered them.
  void Render(Voice::Frame* frames, size_t size, uint32_t block_start);
  inline bool instrument_on() const {
    return instrument_ != RIG_INSTRUMENT_OFF;
  }

  // Cycle counts of the sections of the duo voice (the start, then the end
  // of each).
  inline void Mark(int section) {
#ifndef TEST
    const uint32_t now = DWT->CYCCNT;
#else
    const uint32_t now = 0;
#endif  // TEST
    if (section >= 0 && !sections_frozen_) {
      const uint32_t cycles = now - mark_;
      if (cycles > section_cycles_[section]) {
        section_cycles_[section] = cycles;
      }
    }
    mark_ = now;
  }

  // Probes, placed in the code to time parts of it (with RIG_PROBE, see
  // probe.h): Probe(-1) starts, Probe(i) adds the cycles since the previous
  // probe to probe i (the most cycles, their sum and the number of times,
  // since the previous frame).
  inline void Probe(int i) {
#ifndef TEST
    const uint32_t now = DWT->CYCCNT;
#else
    const uint32_t now = 0;
#endif  // TEST
    if (i >= 0) {
      const uint32_t cycles = now - probe_mark_;
      if (cycles > probe_max_[i]) {
        probe_max_[i] = cycles;
      }
      probe_sum_[i] += cycles;
      ++probe_count_[i];
    }
    probe_mark_ = now;
  }

  // At the end of each block: its cycles, for the statistics of the model,
  // as in the release firmware: without the blocks in which a command was
  // executed, nor the cycles of the decoder of the commands (overrun: more
  // than kRigBlockCycles of them).
  void EndBlock(int duo_engine, uint32_t cycles);

  inline bool engine_forced() const { return engine_forced_; }
  inline bool update_requested() const { return update_requested_; }
  inline bool restart_requested() const { return restart_requested_; }
  // Never return: the updater starts the new firmware; the module restarts.
  void StartUpdater();
  void Restart();
  inline uint16_t telemetry_interval() const { return telemetry_interval_; }
  inline bool frame_requested() const { return frame_requested_; }
  inline void clear_frame_request() { frame_requested_ = false; }

  // For the telemetry.
  inline uint32_t commands() const { return commands_; }
  inline uint32_t command_errors() const { return command_errors_; }
  inline uint8_t last_command() const { return buffer_[0]; }
  inline uint8_t instrument() const { return instrument_; }
  inline const uint32_t* section_cycles() const { return section_cycles_; }
  inline const uint32_t* engine_cycles() const { return engine_cycles_; }
  inline const uint16_t* engine_overruns() const { return engine_overruns_; }
  inline const uint32_t* probe_max() const { return probe_max_; }
  inline const uint32_t* probe_sum() const { return probe_sum_; }
  inline const uint32_t* probe_count() const { return probe_count_; }
  inline void ClearSections() {
    if (!sections_frozen_) {
      std::fill(&section_cycles_[0], &section_cycles_[RIG_SECTION_LAST], 0u);
    }
    std::fill(&probe_max_[0], &probe_max_[kRigNumProbes], 0u);
    std::fill(&probe_sum_[0], &probe_sum_[kRigNumProbes], 0u);
    std::fill(&probe_count_[0], &probe_count_[kRigNumProbes], 0u);
  }

 private:
  void Receive(const int16_t* readings);
  void Symbol(int level);
  void Execute();

  Patch* patch_;

  // Command reception.
  int32_t plateau_;  // Reading of the level being read.
  int stable_;  // Blocks at this level.
  int rest_length_;  // Blocks at rest (until kRestBlocks).
  int rest_before_;  // The same, when FM last left the rest.
  int away_;  // Blocks away from the rest.
  int32_t rest_;  // Reading of FM at rest (-2V), and its span to the start.
  int32_t span_;
  bool rest_known_;
  int last_level_;  // Of the last symbol.
  int idle_;  // Blocks since the last symbol.
  bool receiving_;
  int bits_;
  int num_bits_;
  int length_;
  uint8_t buffer_[kRigMaxArguments + 3];
  uint32_t commands_;
  uint32_t command_errors_;
  uint32_t commands_counted_;  // Executed before the current block.
  uint32_t decoder_cycles_;  // Of the current block.

  // Overrides.
  uint16_t pot_override_[8];
  int16_t cv_override_[8];
  uint8_t cv_override_mask_;
  uint8_t patched_mask_;
  uint8_t patched_state_;
  uint8_t button_mask_;
  int button_blocks_;  // Blocks until the buttons are released, -1: held.

  // Telemetry.
  uint16_t telemetry_interval_;
  int frame_countdown_;  // Blocks until the frame requested, 0: none.
  bool frame_requested_;

  // Debug instrument.
  uint8_t instrument_;
  uint16_t argument_[3];
  uint32_t phase_;
  int16_t readings_[8];

  // Measurements.
  uint32_t mark_;
  uint32_t section_cycles_[RIG_SECTION_LAST];
  bool sections_frozen_;
  uint32_t engine_cycles_[kRigNumEngines];
  uint16_t engine_overruns_[kRigNumEngines];
  uint32_t probe_mark_;
  uint32_t probe_max_[kRigNumProbes];
  uint32_t probe_sum_[kRigNumProbes];
  uint32_t probe_count_[kRigNumProbes];

  bool engine_forced_;
  // Set by a command, read by the main loop.
  volatile bool update_requested_;
  volatile bool restart_requested_;

  DISALLOW_COPY_AND_ASSIGN(Rig);
};

extern Rig rig;

}  // namespace plaits

#endif  // PLAITS_RIG_RIG_H_
