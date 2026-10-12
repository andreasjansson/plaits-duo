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

#ifndef PLAITS_UI_H_
#define PLAITS_UI_H_

#include "stmlib/stmlib.h"

#include "plaits/drivers/cv_adc.h"
#include "plaits/drivers/leds.h"
#include "plaits/drivers/normalization_probe.h"
#include "plaits/drivers/pots_adc.h"
#include "plaits/drivers/switches.h"
#include "plaits/dsp/voice.h"
#include "plaits/pot_controller.h"
#include "plaits/settings.h"

namespace plaits {

class Telemetry;

const int kNumNormalizedChannels = 5;
const int kProbeSequenceDuration = 32;

enum UiMode {
  UI_MODE_NORMAL,
  UI_MODE_DISPLAY_ALTERNATE_PARAMETERS,
  UI_MODE_DISPLAY_OCTAVE,
  UI_MODE_CALIBRATION_C1,
  UI_MODE_CALIBRATION_C3,
  UI_MODE_ERROR
};

class Ui {
 public:
  Ui() { }
  ~Ui() { }
  
  void Init(Patch* patch, Modulations* modulations, Settings* settings);
  
  void Poll();
  
  void set_active_engine(int active_engine) {
    active_engine_ = active_engine;
  }
  
  // While LEVEL is calibrated, the module outputs two voltages in turn on
  // OUT and AUX: the sample to output instead of the voice (0: none).
  inline int16_t calibration_output() const {
    return calibration_output_;
  }

 private:
  void UpdateLEDs();
  void ReadSwitches();
  void ProcessPotsHiddenParameters();
  void LoadState();
  void SaveState();
  void DetectNormalization();
  
  void Navigate(int button);
  void ToggleDuoMode();

  void StartCalibration(bool level);
  void CalibrateC1();
  void CalibrateC3();
  void TrackCalibrationOutput();
  void EndLevelCalibration(UiMode mode);
  void UpdateLevelNoteScale();

  void RealignPots() {
    pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Realign();
    pots_[POTS_ADC_CHANNEL_MORPH_POT].Realign();
    pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Realign();
  }
  
  UiMode mode_;
  
  CvAdc cv_adc_;
  PotsAdc pots_adc_;
  Leds leds_;
  Switches switches_;
  
  int ui_task_;

  float transposition_;
  float octave_;
  Patch* patch_;
  Modulations* modulations_;
  NormalizationProbe normalization_probe_;
  PotController pots_[POTS_ADC_CHANNEL_LAST];
  float pitch_lp_;
  float pitch_lp_calibration_;
  
  // LEVEL used as a pitch input in duo mode.
  float level_pitch_lp_;
  bool calibrating_level_;
  
  // Semitones per unit of LEVEL CV, until LEVEL is calibrated (see
  // Ui::UpdateLevelNoteScale).
  float level_note_scale_;
  
  // The calibration of LEVEL against V/OCT: the phase of the alternation
  // between the two output voltages, the readings of V/OCT and LEVEL at each
  // voltage (averaged), and the notes of the two voltages read by V/OCT.
  int calibration_phase_;
  int16_t calibration_output_;
  float calibration_reading_[2][2];
  float calibration_note_[2];
  
  // LED animation played when entering or leaving duo mode.
  int duo_animation_;
  
  Settings* settings_;
  
  int normalization_detection_count_;
  int normalization_detection_mismatches_[kNumNormalizedChannels];
  uint32_t normalization_probe_state_;

  int pwm_counter_;
  int press_time_[SWITCH_LAST];
  bool ignore_release_[SWITCH_LAST];
  
  int active_engine_;
  
  float cv_c1_;  // For calibration
  
  static const CvAdcChannel normalized_channels_[kNumNormalizedChannels];
  
  // The rig firmware reports the readings and the calibration of the inputs
  // (see plaits/rig/telemetry.h).
  friend class Telemetry;
    
  DISALLOW_COPY_AND_ASSIGN(Ui);
};

}  // namespace plaits

#endif  // PLAITS_UI_H_
