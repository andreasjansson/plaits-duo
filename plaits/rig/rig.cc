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
// The control of the rig firmware.

#include "plaits/rig/rig.h"

#include <algorithm>
#include <cstdlib>

#include "stmlib/dsp/dsp.h"

#include "plaits/resources.h"

#ifndef TEST
#include "stmlib/system/bootloader_utils.h"
#endif  // TEST

namespace plaits {

using namespace std;

// The levels of a command, evenly spaced from the rest (between symbols) to
// the start: then 2 bits each.
static const int kNumLevels = 6;
static const int kLevelGap = 0;
static const int kLevelStart = 5;

// Readings: the most a level may change while it lasts, the least change from
// the rest to the start (6V at FM is 24576 at -0.125 per volt, 39321 at -0.2;
// the normalization probe's 2V, 8192 or 13107), and how far from its level a
// symbol may be (in 1/100 of the spacing of the levels).
static const int32_t kPlateauJitter = 400;
static const int32_t kMinSpan = 18000;
static const int32_t kLevelTolerance = 35;

// Blocks the rest has to last before a start (10ms: the normalization probe,
// when FM is unpatched, changes every 1ms, and the rests between the symbols
// of a command last 2.5ms).
static const int kRestBlocks = 40;

// Blocks a level has to last to be a symbol (1ms), and without a symbol
// to give up a command (100ms).
static const int kStableBlocks = 4;
static const int kTimeoutBlocks = 400;

static const uint32_t kUpdaterAddress = 0x0803c000;

// 2^32 / 47872 (the sample rate): the phase increment of 1Hz.
static const uint32_t kPhasePerHertz = 89718;

void Rig::Init(Patch* patch) {
  patch_ = patch;
  plateau_ = rest_ = span_ = 0;
  rest_known_ = false;
  stable_ = rest_length_ = rest_before_ = away_ = idle_ = 0;
  last_level_ = -1;
  receiving_ = false;
  bits_ = num_bits_ = length_ = 0;
  fill(&buffer_[0], &buffer_[kRigMaxArguments + 3], 0);
  commands_ = command_errors_ = 0;
  fill(&pot_override_[0], &pot_override_[8], 0xffff);
  fill(&cv_override_[0], &cv_override_[8], 0);
  fill(&readings_[0], &readings_[8], 0);
  cv_override_mask_ = patched_mask_ = patched_state_ = button_mask_ = 0;
  button_blocks_ = 0;
  telemetry_interval_ = 8000;
  frame_countdown_ = 0;
  frame_requested_ = false;
  instrument_ = RIG_INSTRUMENT_OFF;
  fill(&argument_[0], &argument_[3], 0);
  phase_ = 0;
  mark_ = 0;
  probe_mark_ = 0;
  commands_counted_ = 0;
  decoder_cycles_ = 0;
  sections_frozen_ = false;
  ClearSections();
  fill(&engine_cycles_[0], &engine_cycles_[kRigNumEngines], 0);
  fill(&engine_overruns_[0], &engine_overruns_[kRigNumEngines], 0);
  engine_forced_ = false;
  update_requested_ = restart_requested_ = false;
}

void Rig::ProcessCommands(const int16_t* readings) {
#ifndef TEST
  const uint32_t start = DWT->CYCCNT;
#endif  // TEST
  Receive(readings);
#ifndef TEST
  decoder_cycles_ = DWT->CYCCNT - start;
#endif  // TEST
}

void Rig::Receive(const int16_t* readings) {
  for (int i = 0; i < 8; ++i) {
    readings_[i] = readings[i];
  }
  const int16_t fm = readings[2];
  if (button_blocks_ > 0 && --button_blocks_ == 0) {
    button_mask_ = 0;
  }
  if (frame_countdown_ > 0 && --frame_countdown_ == 0) {
    frame_requested_ = true;
  }

  // How long FM has been at rest: through glitches (shorter than a level),
  // and until it leaves for a level.
  if (abs(int32_t(fm) - rest_) <= kPlateauJitter) {
    away_ = 0;
    if (rest_length_ < kRestBlocks) {
      ++rest_length_;
    }
  } else if (away_ < kStableBlocks && ++away_ == 1) {
    rest_before_ = rest_length_;
  } else if (away_ >= kStableBlocks) {
    rest_length_ = 0;
  }

  // A level is a plateau of the reading, read once (a reading between two
  // levels, while FM changes, is not a level).
  if (abs(int32_t(fm) - plateau_) > kPlateauJitter) {
    plateau_ = fm;
    stable_ = 0;
  } else if (stable_ < kTimeoutBlocks && ++stable_ == kStableBlocks) {
    // The reading decreases as the voltage increases.
    const int32_t rise = rest_ - plateau_;
    if (!receiving_) {
      if (rest_known_ && rise > kMinSpan && rest_before_ >= kRestBlocks) {
        span_ = rise;
        idle_ = 0;
        last_level_ = kLevelStart;
        Symbol(kLevelStart);
      } else if (!rest_known_ || abs(plateau_ - rest_) > kPlateauJitter) {
        // A new rest.
        rest_ = plateau_;
        rest_known_ = true;
        rest_length_ = kStableBlocks;
        away_ = 0;
      }
    } else {
      const int32_t scaled = rise * (kNumLevels - 1) * 100 / span_;
      const int level = (scaled + 50) / 100;
      if (level < 0 || level >= kNumLevels || \
          abs(scaled - level * 100) > kLevelTolerance) {
        receiving_ = false;
        ++command_errors_;
      } else if (level != last_level_) {
        // (A glitch splits a level into two: the same symbol, once. A gap
        // separates the symbols of a command.)
        last_level_ = level;
        idle_ = 0;
        Symbol(level);
      }
    }
  }
  if (receiving_ && ++idle_ > kTimeoutBlocks) {
    receiving_ = false;
    ++command_errors_;
  }
}

void Rig::Symbol(int level) {
  if (level == kLevelStart) {
    receiving_ = true;
    bits_ = num_bits_ = length_ = 0;
    return;
  }
  if (level == kLevelGap || !receiving_) {
    return;
  }
  bits_ = (bits_ << 2) | (level - 1);
  num_bits_ += 2;
  if (num_bits_ < 8) {
    return;
  }
  buffer_[length_++] = bits_;
  bits_ = num_bits_ = 0;
  if (length_ >= 2 && buffer_[1] > kRigMaxArguments) {
    receiving_ = false;
    ++command_errors_;
  } else if (length_ >= 2 && length_ == buffer_[1] + 3) {
    receiving_ = false;
    uint8_t check = 0;
    for (int i = 0; i < length_ - 1; ++i) {
      check += buffer_[i];
    }
    if ((check ^ 0xa5) == buffer_[length_ - 1]) {
      ++commands_;
      Execute();
    } else {
      ++command_errors_;
    }
  }
}

void Rig::Execute() {
  const uint8_t* a = &buffer_[2];
  const int n = buffer_[1];
  const uint16_t word = n >= 3 ? (a[1] << 8) | a[2] : 0;
  switch (buffer_[0]) {
    case RIG_COMMAND_BUTTONS:
      button_mask_ = a[0] & 3;
      button_blocks_ = a[1] ? a[1] * 16 : -1;
      break;

    case RIG_COMMAND_ENGINE:
      engine_forced_ = a[0] < 16;
      if (engine_forced_) {
        patch_->duo_mode = true;
        patch_->duo_engine = a[0];
      }
      break;

    case RIG_COMMAND_KNOB:
      if (a[0] < 8) {
        pot_override_[a[0]] = word;
      }
      break;

    case RIG_COMMAND_CV:
      if (a[0] < 8) {
        cv_override_[a[0]] = static_cast<int16_t>(word);
        if (a[3]) {
          cv_override_mask_ |= 1 << a[0];
        } else {
          cv_override_mask_ &= ~(1 << a[0]);
        }
      }
      break;

    case RIG_COMMAND_PATCHED:
      patched_mask_ = a[0];
      patched_state_ = a[1];
      break;

    case RIG_COMMAND_TELEMETRY:
      telemetry_interval_ = (a[0] << 8) | a[1];
      frame_countdown_ = a[2] * 16;
      break;

    case RIG_COMMAND_RESET:
      if (n >= 1 && a[0] == 1) {
        // Until the next reset: the sections of a model, read after switching
        // to a lighter one (a model which overruns garbles the frames).
        sections_frozen_ = true;
        break;
      }
      sections_frozen_ = false;
      ClearSections();
      fill(&engine_cycles_[0], &engine_cycles_[kRigNumEngines], 0);
      fill(&engine_overruns_[0], &engine_overruns_[kRigNumEngines], 0);
      break;

    case RIG_COMMAND_INSTRUMENT:
      instrument_ = a[0];
      for (int i = 0; i < 3; ++i) {
        argument_[i] = (a[1 + 2 * i] << 8) | a[2 + 2 * i];
      }
      break;

    case RIG_COMMAND_UPDATE:
      update_requested_ = a[0] == 0x55 && a[1] == 0xaa;
      restart_requested_ = a[0] == 0x5a && a[1] == 0xa5;
      if (restart_requested_) {
        // At once (in the audio interrupt): it also works when the main loop
        // never runs (the interrupt using all the processor).
        Restart();
      }
      break;
  }
}

void Rig::ApplyPatched(Modulations* modulations) const {
  bool* patched = &modulations->frequency_patched;
  // FM carries the commands.
  patched[0] = false;
  for (int i = 0; i < 5; ++i) {
    if (patched_mask_ & (1 << i)) {
      patched[i] = (patched_state_ >> i) & 1;
    }
  }
}

void Rig::Render(Voice::Frame* frames, size_t size, uint32_t block_start) {
  switch (instrument_) {
    case RIG_INSTRUMENT_SINE:
      {
        const uint32_t increment = argument_[0] * kPhasePerHertz;
        for (size_t i = 0; i < size; ++i) {
          phase_ += increment;
          const float phase = static_cast<float>(phase_) / 4294967296.0f;
          frames[i].out = static_cast<short>(stmlib::InterpolateWrap(
              lut_sine, phase, 1024.0f) * static_cast<float>(argument_[1]));
          frames[i].aux = static_cast<short>(stmlib::InterpolateWrap(
              lut_sine, phase + 0.75f, 1024.0f) * \
              static_cast<float>(argument_[2]));
        }
      }
      break;

    case RIG_INSTRUMENT_CONSTANT:
      for (size_t i = 0; i < size; ++i) {
        frames[i].out = static_cast<short>(argument_[0]);
        frames[i].aux = static_cast<short>(argument_[1]);
      }
      break;

    case RIG_INSTRUMENT_READINGS:
      for (size_t i = 0; i < size; ++i) {
        frames[i].out = readings_[argument_[0] & 7];
        frames[i].aux = readings_[argument_[1] & 7];
      }
      break;

    case RIG_INSTRUMENT_BURN:
#ifndef TEST
      while (DWT->CYCCNT - block_start < argument_[0]) { }
#endif  // TEST
      break;
  }
}

void Rig::EndBlock(int duo_engine, uint32_t cycles) {
  if (commands_ != commands_counted_) {
    // A command took part of the block (the release firmware has none).
    commands_counted_ = commands_;
    return;
  }
  // Nor the decoder of the commands.
  cycles -= min(cycles, decoder_cycles_);
  const bool overrun = cycles > kRigBlockCycles;
  duo_engine &= kRigNumEngines - 1;
  if (cycles > engine_cycles_[duo_engine]) {
    engine_cycles_[duo_engine] = cycles;
  }
  if (overrun && engine_overruns_[duo_engine] != 0xffff) {
    ++engine_overruns_[duo_engine];
  }
}

void Rig::Restart() {
#ifndef TEST
  NVIC_SystemReset();
#endif  // TEST
}

void Rig::StartUpdater() {
#ifndef TEST
  // Stops everything which could interrupt the updater or write into its
  // memory: the interrupts, the DMA channels (of the codec and the ADCs),
  // and the peripherals. Then the same as the bootloader does before
  // starting the application.
  __disable_irq();
  DMA_Channel_TypeDef* channels[] = {
    DMA1_Channel1, DMA1_Channel2, DMA1_Channel3, DMA1_Channel4,
    DMA1_Channel5, DMA1_Channel6, DMA1_Channel7,
    DMA2_Channel1, DMA2_Channel2, DMA2_Channel3, DMA2_Channel4,
    DMA2_Channel5
  };
  for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); ++i) {
    channels[i]->CCR = 0;
  }
  DMA1->IFCR = 0x0fffffff;
  DMA2->IFCR = 0x000fffff;
  RCC->APB1RSTR = 0xffffffff;
  RCC->APB1RSTR = 0;
  RCC->APB2RSTR = 0xffffffff;
  RCC->APB2RSTR = 0;
  stmlib::Uninitialize();
  IWDG_ReloadCounter();  // It keeps running in the updater.
  __enable_irq();
  stmlib::JumpTo(kUpdaterAddress);
#endif  // TEST
}

}  // namespace plaits
