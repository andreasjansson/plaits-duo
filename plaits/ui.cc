// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
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
// UI and CV processing ("controller" and "view")

#include "plaits/ui.h"


#ifdef PLAITS_RIG
#include "plaits/rig/rig.h"
#endif  // PLAITS_RIG

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/system/system_clock.h"

namespace plaits {
  
using namespace std;
using namespace stmlib;

static const int32_t kLongPressTime = 2000;

// In ms (the LEDs are updated at 1kHz).
static const int kDuoAnimationStepDuration = 80;
static const int kDuoAnimationDuration = 4 * kDuoAnimationStepDuration;

#define ENABLE_LFO_MODE

void Ui::Init(Patch* patch, Modulations* modulations, Settings* settings) {
  patch_ = patch;
  modulations_ = modulations;
  settings_ = settings;

  cv_adc_.Init();
  pots_adc_.Init();
  leds_.Init();
  switches_.Init();

  ui_task_ = 0;
  mode_ = UI_MODE_NORMAL;
  
  LoadState();
  
  if (switches_.pressed_immediate(SWITCH_ROW_2)) {
    State* state = settings_->mutable_state();
    if (state->color_blind == 1) {
      state->color_blind = 0; 
    } else {
      state->color_blind = 1; 
    }
    settings_->SaveState();
  }
  
  // Bind pots to parameters.
  pots_[POTS_ADC_CHANNEL_FREQ_POT].Init(
      &transposition_, NULL, 2.0f, -1.0f);
  pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Init(
      &patch->harmonics, &octave_, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Init(
      &patch->timbre, &patch->lpg_colour, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_MORPH_POT].Init(
      &patch->morph, &patch->decay, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_TIMBRE_ATTENUVERTER].Init(
      &patch->timbre_modulation_amount, NULL, 2.0f, -1.0f);
  pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].Init(
      &patch->frequency_modulation_amount, NULL, 2.0f, -1.0f);
  pots_[POTS_ADC_CHANNEL_MORPH_ATTENUVERTER].Init(
      &patch->morph_modulation_amount, NULL, 2.0f, -1.0f);
  
  // Keep track of the agreement between the random sequence sent to the 
  // switch and the value read by the ADC.
  normalization_detection_count_ = 0;
  normalization_probe_state_ = 0;
  normalization_probe_.Init();
  fill(
      &normalization_detection_mismatches_[0],
      &normalization_detection_mismatches_[5],
      0);
  
  pwm_counter_ = 0;
  fill(&press_time_[0], &press_time_[SWITCH_LAST], 0);
  fill(&ignore_release_[0], &ignore_release_[SWITCH_LAST], false);
  
  active_engine_ = 0;
  cv_c1_ = 0.0f;
  pitch_lp_ = 0.0f;
  pitch_lp_calibration_ = 0.0f;
  level_pitch_lp_ = 0.0f;
  calibrating_level_ = false;
  calibration_phase_ = 0;
  calibration_output_ = 0;
  fill(&calibration_reading_[0][0], &calibration_reading_[0][0] + 4, 0.0f);
  fill(&calibration_note_[0], &calibration_note_[2], 0.0f);
  duo_animation_ = 0;
}

void Ui::LoadState() {
  const State& state = settings_->state();
  patch_->engine = state.engine;
  patch_->duo_mode = state.duo_mode;
  patch_->duo_engine = state.duo_engine;
  UpdateLevelNoteScale();
  patch_->lpg_colour = static_cast<float>(state.lpg_colour) / 256.0f;
  patch_->decay = static_cast<float>(state.decay) / 256.0f;
  octave_ = static_cast<float>(state.octave) / 256.0f;
}

void Ui::SaveState() {
  State* state = settings_->mutable_state();
  state->engine = patch_->engine;
  state->duo_mode = patch_->duo_mode;
  state->duo_engine = patch_->duo_engine;
  state->lpg_colour = static_cast<uint8_t>(patch_->lpg_colour * 256.0f);
  state->decay = static_cast<uint8_t>(patch_->decay * 256.0f);
  state->octave = static_cast<uint8_t>(octave_ * 256.0f);
  settings_->SaveState();
}

void Ui::UpdateLEDs() {
  leds_.Clear();
  ++pwm_counter_;
  
  int pwm_counter = pwm_counter_ & 15;
  int triangle = (pwm_counter_ >> 4) & 31;
  triangle = triangle < 16 ? triangle : 31 - triangle;

  switch (mode_) {
    case UI_MODE_NORMAL:
      {
        LedColor red = settings_->state().color_blind == 1
            ? ((pwm_counter & 7) ? LED_COLOR_OFF : LED_COLOR_YELLOW)
            : LED_COLOR_RED;
        LedColor green = settings_->state().color_blind == 1
            ? LED_COLOR_YELLOW
            : LED_COLOR_GREEN;
        if (patch_->duo_mode) {
          // In duo mode, the models of the stereo layout are yellow (a dim
          // yellow in the colour-blind mode, which shows green as yellow),
          // those of the split layout red.
          green = settings_->state().color_blind == 1
              ? (pwm_counter < 6 ? LED_COLOR_YELLOW : LED_COLOR_OFF)
              : LED_COLOR_YELLOW;
        }
        const int selected_engine = patch_->duo_mode
            ? patch_->duo_engine
            : patch_->engine;
        
        if (duo_animation_) {
          // Entering duo mode: a pair of LEDs moves from the center to the
          // edges. Leaving duo mode: from the edges to the center.
          --duo_animation_;
          int step = 3 - duo_animation_ / kDuoAnimationStepDuration;
          int distance = patch_->duo_mode ? step : 3 - step;
          leds_.set(3 - distance, LED_COLOR_YELLOW);
          leds_.set(4 + distance, LED_COLOR_YELLOW);
          break;
        }
        
        leds_.set(
            active_engine_ & 7,
            active_engine_ & 8 ? red : green);
        if (pwm_counter < triangle) {
          leds_.mask(
              selected_engine & 7,
              selected_engine & 8 ? red : green);
        }
      }
      break;
    
    case UI_MODE_DISPLAY_ALTERNATE_PARAMETERS:
      {
        for (int parameter = 0; parameter < 2; ++parameter) {
          float value = parameter == 0
              ? patch_->lpg_colour
              : patch_->decay;
          value -= 0.001f;
          for (int i = 0; i < 4; ++i) {
            leds_.set(
                parameter * 4 + 3 - i,
                value * 64.0f > pwm_counter ? LED_COLOR_YELLOW : LED_COLOR_OFF);
            value -= 0.25f;
          }
        }
      }
      break;
    
    case UI_MODE_DISPLAY_OCTAVE:
      {
#ifdef ENABLE_LFO_MODE
        int octave = static_cast<float>(octave_ * 10.0f);
        for (int i = 0; i < 8; ++i) {
          LedColor color = LED_COLOR_OFF;
          if (octave == 0) {
            color = i == (triangle >> 1) ? LED_COLOR_OFF : LED_COLOR_YELLOW;
          } else if (octave == 9) {
            color = LED_COLOR_YELLOW;
          } else {
            color = (octave - 1) == i ? LED_COLOR_YELLOW : LED_COLOR_OFF;
          }
          leds_.set(7 - i, color);
        }
#else
        int octave = static_cast<float>(octave_ * 9.0f);
        for (int i = 0; i < 8; ++i) {
          leds_.set(
              7 - i,
              octave == i || (octave == 8) ? LED_COLOR_YELLOW : LED_COLOR_OFF);
        }
#endif  // ENABLE_LFO_MODE
      }
      break;
      
    case UI_MODE_CALIBRATION_C1:
    case UI_MODE_CALIBRATION_C3:
      {
        // Calibrating LEVEL: a quick green flash when the input of the step
        // reads the two voltages, settled (the button can be pressed).
        // Otherwise a slow pulse, green for V/OCT, yellow for LEVEL.
        const int step = mode_ == UI_MODE_CALIBRATION_C1 ? 0 : 1;
        if (calibrating_level_ && CalibrationReady(step)) {
          if ((pwm_counter_ >> 7) & 1) {
            leds_.set(7, LED_COLOR_GREEN);
          }
        } else if (pwm_counter < triangle) {
          leds_.set(
              calibrating_level_ ? 7 : 0,
              step == 0 ? LED_COLOR_GREEN : LED_COLOR_YELLOW);
        }
      }
      break;
    
    case UI_MODE_ERROR:
      if (pwm_counter < triangle) {
        for (int i = 0; i < kNumLEDs; ++i) {
          leds_.set(i, LED_COLOR_RED);
        }
      }
      break;
  }
  leds_.Write();
}

void Ui::Navigate(int button) {
  RealignPots();
  int* engine = patch_->duo_mode ? &patch_->duo_engine : &patch_->engine;
  if (button == 0) {
    if (*engine >= 8) {
      *engine = *engine & 7;
    } else {
      *engine = (*engine + 1) % 8;
    }
  } else {
    if (*engine < 8) {
      *engine = (*engine & 7) + 8;
    } else {
      *engine = 8 + ((*engine + 1) % 8);
    }
  }
  SaveState();
}

void Ui::ToggleDuoMode() {
  RealignPots();
  patch_->duo_mode = !patch_->duo_mode;
  duo_animation_ = kDuoAnimationDuration;
  SaveState();
}

void Ui::ReadSwitches() {
  switches_.Debounce();
  
  switch (mode_) {
    case UI_MODE_NORMAL:
      {
        for (int i = 0; i < SWITCH_LAST; ++i) {
          if (switches_.just_pressed(Switch(i))) {
            press_time_[i] = 0;
            ignore_release_[i] = false;
          }
          if (switches_.pressed(Switch(i))) {
            ++press_time_[i];
          } else {
            press_time_[i] = 0;
          }
        }
        
        if (switches_.just_pressed(Switch(0))) {
          pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Lock();
          pots_[POTS_ADC_CHANNEL_MORPH_POT].Lock();
        }
        if (switches_.just_pressed(Switch(1))) {
          pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Lock();
        }
        
        if (pots_[POTS_ADC_CHANNEL_MORPH_POT].editing_hidden_parameter() ||
            pots_[POTS_ADC_CHANNEL_TIMBRE_POT].editing_hidden_parameter()) {
          mode_ = UI_MODE_DISPLAY_ALTERNATE_PARAMETERS;
        }
        
        if (pots_[POTS_ADC_CHANNEL_HARMONICS_POT].editing_hidden_parameter()) {
          mode_ = UI_MODE_DISPLAY_OCTAVE;
        }
        
        // Long, double press: enter calibration mode.
        if (press_time_[0] >= kLongPressTime &&
            press_time_[1] >= kLongPressTime) {
          press_time_[0] = press_time_[1] = 0;
          RealignPots();
          // In duo mode, the LEVEL input is calibrated instead of V/OCT.
          StartCalibration(patch_->duo_mode);
        }
        
        // Long press or actually editing any hidden parameter: display value
        // of hidden parameters.
        if (press_time_[0] >= kLongPressTime && !press_time_[1]) {
          press_time_[0] = press_time_[1] = 0;
          mode_ = UI_MODE_DISPLAY_ALTERNATE_PARAMETERS;
        }
        if (press_time_[1] >= kLongPressTime && !press_time_[0]) {
          press_time_[0] = press_time_[1] = 0;
          mode_ = UI_MODE_DISPLAY_OCTAVE;
        }
        
        // A button released while the other one is still held (or is
        // released at the same time): toggle duo mode, and ignore the release
        // of the other button.
        bool released[SWITCH_LAST];
        for (int i = 0; i < SWITCH_LAST; ++i) {
          released[i] = switches_.released(Switch(i)) && !ignore_release_[i];
        }
        bool both = false;
        for (int i = 0; i < SWITCH_LAST; ++i) {
          const int other = 1 - i;
          if (released[i] && (press_time_[other] > 0 || released[other])) {
            both = true;
          }
        }
        if (both) {
          ignore_release_[0] = ignore_release_[1] = true;
          ToggleDuoMode();
        } else if (released[0]) {
          Navigate(0);
        } else if (released[1]) {
          Navigate(1);
        }
      }
      break;
      
    case UI_MODE_DISPLAY_ALTERNATE_PARAMETERS:
    case UI_MODE_DISPLAY_OCTAVE:
      for (int i = 0; i < SWITCH_LAST; ++i) {
        if (switches_.released(Switch(i))) {
          pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_MORPH_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Unlock();
          press_time_[i] = 0;
          mode_ = UI_MODE_NORMAL;
        }
      }
      break;
    
    case UI_MODE_CALIBRATION_C1:
      for (int i = 0; i < SWITCH_LAST; ++i) {
        if (switches_.just_pressed(Switch(i))) {
          press_time_[i] = 0;
          ignore_release_[i] = true;
          CalibrateC1();
          break;
        }
      }
      break;
      
    case UI_MODE_CALIBRATION_C3:
      for (int i = 0; i < SWITCH_LAST; ++i) {
        if (switches_.just_pressed(Switch(i))) {
          press_time_[i] = 0;
          ignore_release_[i] = true;
          CalibrateC3();
          break;
        }
      }
      break;

    case UI_MODE_ERROR:
      for (int i = 0; i < SWITCH_LAST; ++i) {
        if (switches_.just_pressed(Switch(i))) {
          press_time_[i] = 0;
          ignore_release_[i] = true;
          mode_ = UI_MODE_NORMAL;
        }
      }
      break;
  }
}

void Ui::ProcessPotsHiddenParameters() {
  for (int i = 0; i < POTS_ADC_CHANNEL_LAST; ++i) {
    pots_[i].ProcessUIRate();
  }
}

/* static */
const CvAdcChannel Ui::normalized_channels_[] = {
  CV_ADC_CHANNEL_FM,
  CV_ADC_CHANNEL_TIMBRE,
  CV_ADC_CHANNEL_MORPH,
  CV_ADC_CHANNEL_TRIGGER,
  CV_ADC_CHANNEL_LEVEL,
};

void Ui::DetectNormalization() {
  bool expected_value = normalization_probe_state_ >> 31;
  for (int i = 0; i < kNumNormalizedChannels; ++i) {
    CvAdcChannel channel = normalized_channels_[i];
    bool read_value = cv_adc_.value(channel) < \
        settings_->calibration_data(channel).normalization_detection_threshold;
    if (expected_value != read_value) {
      ++normalization_detection_mismatches_[i];
    }
  }
  
  ++normalization_detection_count_;
  if (normalization_detection_count_ == kProbeSequenceDuration) {
    normalization_detection_count_ = 0;
    bool* destination = &modulations_->frequency_patched;
    for (int i = 0; i < kNumNormalizedChannels; ++i) {
      destination[i] = normalization_detection_mismatches_[i] >= 2;
      normalization_detection_mismatches_[i] = 0;
    }
#ifdef PLAITS_RIG
    rig.ApplyPatched(modulations_);
#endif  // PLAITS_RIG
  }

  normalization_probe_state_ = 1103515245 * normalization_probe_state_ + 12345;
  normalization_probe_.Write(normalization_probe_state_ >> 31);
}

void Ui::Poll() {
#ifdef PLAITS_RIG
  int16_t readings[CV_ADC_CHANNEL_LAST];
  for (int i = 0; i < CV_ADC_CHANNEL_LAST; ++i) {
    readings[i] = cv_adc_.raw_value(CvAdcChannel(i));
  }
  rig.ProcessCommands(readings);
#endif  // PLAITS_RIG
  for (int i = 0; i < POTS_ADC_CHANNEL_LAST; ++i) {
    pots_[i].ProcessControlRate(pots_adc_.float_value(PotsAdcChannel(i)));
  }
  
  float* destination = &modulations_->engine;
  for (int i = 0; i < CV_ADC_CHANNEL_LAST; ++i) {
    destination[i] = settings_->calibration_data(i).Transform(
        cv_adc_.float_value(CvAdcChannel(i)));
  }
#ifdef PLAITS_RIG
  if (rig.engine_forced()) {
    // The model given by a command, without the MODEL CV.
    modulations_->engine = 0.0f;
  }
#endif  // PLAITS_RIG
  
  ONE_POLE(pitch_lp_, modulations_->note, 0.7f);
  ONE_POLE(
      pitch_lp_calibration_, cv_adc_.float_value(CV_ADC_CHANNEL_V_OCT), 0.1f);
  modulations_->note = pitch_lp_;
  
  // LEVEL as a pitch CV, for duo mode. Same smoothing as V/OCT.
  const float level = cv_adc_.float_value(CV_ADC_CHANNEL_LEVEL);
  const PersistentData& p = settings_->persistent_data();
  const float level_note = p.level_note_scale != 0.0f
      ? level * p.level_note_scale + p.level_note_offset
      : modulations_->level * level_note_scale_;
  ONE_POLE(level_pitch_lp_, level_note, 0.7f);
  modulations_->level_note = level_pitch_lp_;
  if (calibration_output_) {
    TrackCalibrationOutput();
  }
  
  ui_task_ = (ui_task_ + 1) % 4;
  switch (ui_task_) {
    case 0:
      UpdateLEDs();
      break;
    
    case 1:
      ReadSwitches();
      break;
    
    case 2:
      ProcessPotsHiddenParameters();
      break;
      
    case 3:
      DetectNormalization();
      break;
  }
  
  cv_adc_.Convert();
  pots_adc_.Convert();

#ifdef ENABLE_LFO_MODE
  int octave = static_cast<int>(octave_ * 10.0f);
  if (octave == 0) {
    patch_->note = -48.37f + transposition_ * 60.0f;
  } else if (octave == 9) {
    patch_->note = 60.0f + transposition_ * 48.0f;
  } else {
    const float fine = transposition_ * 7.0f;
    patch_->note = fine + static_cast<float>(octave) * 12.0f;
  }
#else
  int octave = static_cast<int>(octave_ * 9.0f);
  if (octave < 8) {
    const float fine = transposition_ * 7.0f;
    patch_->note = fine + static_cast<float>(octave) * 12.0f + 12.0f;
  } else {
    patch_->note = 60.0f + transposition_ * 48.0f;
  }
#endif  // ENABLE_LFO_MODE
}

}  // namespace plaits
