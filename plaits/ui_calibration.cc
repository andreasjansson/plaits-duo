// Copyright 2016 Emilie Gillet.
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
// Calibration of the CV inputs: V/OCT (the original firmware's calibration,
// moved from ui.cc), and LEVEL as a pitch input in duo mode. This code only
// runs while calibrating, and is optimized for size (see plaits/makefile).
//
// LEVEL, never calibrated at the factory, reads volts as V/OCT does until it
// is calibrated: the two inputs have the same input stage, and the factory
// calibration of V/OCT accounts for what they share (the reference of the
// ADC, the gain of the converters, which is 2.4 to 3.1% too high), though not
// for the tolerances of their resistors. The calibration needs no voltage
// source: holding the two buttons in duo mode, the module outputs two
// voltages in turn on OUT and AUX, which are patched into V/OCT (calibrated
// at the factory), then into LEVEL.

#include "plaits/ui.h"

#include <cmath>

#include "stmlib/dsp/dsp.h"

namespace plaits {

using namespace stmlib;

// The scale of V/OCT (semitones per unit of ADC reading: -60 nominally),
// measured at the factory, the reference for LEVEL, whose input stage is the
// same. The nominal one if it is implausible.
static float VOctScale(const Settings& settings) {
  const float scale = settings.calibration_data(CV_ADC_CHANNEL_V_OCT).scale;
  return scale < -54.0f && scale > -66.0f ? scale : -60.0f;
}

// The two voltages output for the calibration against V/OCT, in turn: about
// 0.5V and 4.4V (the output stage inverts, and has a gain of about 8V at full
// scale). Each is output for half a period of 64 blocks (16ms), and the
// readings of its first 16 blocks are skipped.
static const int16_t kCalibrationSamples[2] = { -2000, -18000 };
static const int kCalibrationPeriod = 64;
static const int kCalibrationSettling = 16;

void Ui::UpdateLevelNoteScale() {
  // Semitones per unit of (calibrated) LEVEL CV, until LEVEL is calibrated:
  // as many per unit of reading as V/OCT.
  level_note_scale_ = VOctScale(*settings_) / \
      settings_->calibration_data(CV_ADC_CHANNEL_LEVEL).scale;
}

void Ui::StartCalibration(bool level) {
  mode_ = UI_MODE_CALIBRATION_C1;
  calibrating_level_ = level;
  normalization_probe_.Disable();
  if (level) {
    calibration_phase_ = 0;
    calibration_output_ = kCalibrationSamples[0];
  }
}

void Ui::TrackCalibrationOutput() {
  const int half = kCalibrationPeriod / 2;
  const int voltage = calibration_phase_ / half;
  if (calibration_phase_ % half >= kCalibrationSettling) {
    ONE_POLE(
        calibration_reading_[voltage][0],
        cv_adc_.float_value(CV_ADC_CHANNEL_V_OCT),
        0.02f);
    ONE_POLE(
        calibration_reading_[voltage][1],
        cv_adc_.float_value(CV_ADC_CHANNEL_LEVEL),
        0.02f);
  }
  calibration_phase_ = (calibration_phase_ + 1) % kCalibrationPeriod;
  calibration_output_ = kCalibrationSamples[calibration_phase_ / half];
}

void Ui::EndLevelCalibration(UiMode mode) {
  mode_ = mode;
  calibration_output_ = 0;
  normalization_probe_.Init();
}

void Ui::CalibrateC1() {
  if (calibrating_level_) {
    // OUT or AUX is patched into V/OCT: the notes of the two voltages.
    const ChannelCalibrationData& c = settings_->calibration_data(
        CV_ADC_CHANNEL_V_OCT);
    for (int i = 0; i < 2; ++i) {
      calibration_note_[i] = c.Transform(calibration_reading_[i][0]);
    }
    // Nominally 47 semitones apart.
    const float interval = calibration_note_[1] - calibration_note_[0];
    if (interval > 30.0f && interval < 64.0f) {
      mode_ = UI_MODE_CALIBRATION_C3;
      // Into LEVEL too (with a stackable cable or a mult): done.
      if (fabsf(calibration_reading_[1][1] - calibration_reading_[0][1]) > \
          0.4f) {
        CalibrateC3();
      }
    } else {
      EndLevelCalibration(UI_MODE_ERROR);
    }
    return;
  }

  // Acquire offsets for all channels.
  for (int i = 0; i < CV_ADC_CHANNEL_LAST; ++i) {
    if (i != CV_ADC_CHANNEL_V_OCT) {
      ChannelCalibrationData* c = settings_->mutable_calibration_data(i);
      c->offset = -cv_adc_.float_value(CvAdcChannel(i)) * c->scale;
    }
  }
  cv_c1_ = pitch_lp_calibration_;
  mode_ = UI_MODE_CALIBRATION_C3;
}

void Ui::CalibrateC3() {
  if (calibrating_level_) {
    // The same output is now patched into LEVEL, which nominally reads -60
    // semitones per unit.
    const float scale = (calibration_note_[1] - calibration_note_[0]) / \
        (calibration_reading_[1][1] - calibration_reading_[0][1]);
    if (scale > -72.0f && scale < -48.0f) {
      PersistentData* p = settings_->mutable_persistent_data();
      p->level_note_scale = scale;
      p->level_note_offset = calibration_note_[0] - \
          scale * calibration_reading_[0][1];
      settings_->SavePersistentData();
      EndLevelCalibration(UI_MODE_NORMAL);
    } else {
      EndLevelCalibration(UI_MODE_ERROR);
    }
    return;
  }

  // (-33/100.0*1 + -33/140.0 * -10.0) / 3.3 * 2.0 - 1 = 0.228
  float c1 = cv_c1_;

  // (-33/100.0*1 + -33/140.0 * -10.0) / 3.3 * 2.0 - 1 = -0.171
  float c3 = pitch_lp_calibration_;
  float delta = c3 - c1;

  if (delta > -0.6f && delta < -0.2f) {
    ChannelCalibrationData* c = settings_->mutable_calibration_data(
        CV_ADC_CHANNEL_V_OCT);
    c->scale = 24.0f / delta;
    c->offset = 12.0f - c->scale * c1;
    settings_->SavePersistentData();
    // LEVEL reads volts as V/OCT does, until it is calibrated.
    UpdateLevelNoteScale();
    mode_ = UI_MODE_NORMAL;
  } else {
    mode_ = UI_MODE_ERROR;
  }
  normalization_probe_.Init();
}

}  // namespace plaits
