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
// at the factory), then into LEVEL. Each step waits for its input to read
// the two voltages, settled (the LED flashes green), and LEVEL must read the
// same voltages as V/OCT: whatever the calibration of V/OCT, LEVEL then reads
// the same pitches.
//
// The calibration of V/OCT (in the original firmware's mode) only accepts
// voltages about 2V apart (the original firmware accepted 1 to 3V), and
// keeps the offsets of the inputs which are patched.

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

// An input reads the two voltages when the difference between its readings
// is that of 2.5 to 5.3V (about 3.9V: 0.8), and they have settled when the
// difference has moved by less than 0.0002 (1.2 cents for the whole
// interval) in each of the last 64 periods (1s, so that the LED's change
// from a pulse to a flash is seen): about 1.2s after the cable is patched.
static const float kCalibrationMinInterval = 0.5f;
static const float kCalibrationMaxInterval = 1.07f;
static const float kCalibrationStableChange = 0.0002f;
static const int kCalibrationStablePeriods = 64;

// LEVEL reads the same voltages as V/OCT: the same difference, within the
// tolerances of the two input stages.
static const float kCalibrationMinRatio = 0.85f;
static const float kCalibrationMaxRatio = 1.18f;

// The calibration of V/OCT: 1V, then 3V, which V/OCT reads 0.41 apart
// (-0.2 per volt, and the converter's gain error, 2.4 to 3.1% too high),
// within 10%.
static const float kVOctMinDelta = -0.46f;
static const float kVOctMaxDelta = -0.37f;

void Ui::UpdateLevelNoteScale() {
  // Semitones per unit of (calibrated) LEVEL CV, until LEVEL is calibrated:
  // as many per unit of reading as V/OCT.
  level_note_scale_ = VOctScale(*settings_) / \
      settings_->calibration_data(CV_ADC_CHANNEL_LEVEL).scale;
}

void Ui::StartCalibration(bool level) {
  mode_ = UI_MODE_CALIBRATION_C1;
  calibrating_level_ = level;
  // (The detection does not work without the probe.)
  calibration_patched_ = 0;
  const bool* patched = &modulations_->frequency_patched;
  for (int i = 0; i < kNumNormalizedChannels; ++i) {
    calibration_patched_ |= patched[i] << i;
  }
  normalization_probe_.Disable();
  if (level) {
    calibration_phase_ = 0;
    calibration_output_ = kCalibrationSamples[0];
    for (int i = 0; i < 2; ++i) {
      calibration_interval_[i] = 0.0f;
      calibration_stable_[i] = 0;
    }
  }
}

void Ui::TrackCalibrationOutput() {
  const int half = kCalibrationPeriod / 2;
  const int voltage = calibration_phase_ / half;
  if (calibration_phase_ % half >= kCalibrationSettling) {
    ONE_POLE(
        calibration_reading_[voltage][0],
        cv_adc_.float_value(CV_ADC_CHANNEL_V_OCT),
        0.05f);
    ONE_POLE(
        calibration_reading_[voltage][1],
        cv_adc_.float_value(CV_ADC_CHANNEL_LEVEL),
        0.05f);
  }
  calibration_phase_ = (calibration_phase_ + 1) % kCalibrationPeriod;
  calibration_output_ = kCalibrationSamples[calibration_phase_ / half];
  if (calibration_phase_ == 0) {
    // A period has ended: have the readings settled?
    for (int i = 0; i < 2; ++i) {
      const float interval = \
          calibration_reading_[1][i] - calibration_reading_[0][i];
      if (fabsf(interval - calibration_interval_[i]) < \
          kCalibrationStableChange) {
        if (calibration_stable_[i] < kCalibrationStablePeriods) {
          ++calibration_stable_[i];
        }
      } else {
        calibration_stable_[i] = 0;
      }
      calibration_interval_[i] = interval;
    }
  }
}

bool Ui::CalibrationReady(int input) const {
  const float interval = fabsf(calibration_interval_[input]);
  return calibration_stable_[input] >= kCalibrationStablePeriods && \
      interval > kCalibrationMinInterval && \
      interval < kCalibrationMaxInterval;
}

void Ui::EndLevelCalibration(UiMode mode) {
  mode_ = mode;
  calibration_output_ = 0;
  normalization_probe_.Init();
}

void Ui::CalibrateC1() {
  if (calibrating_level_) {
    // OUT or AUX is patched into V/OCT, which reads the two voltages,
    // settled: the notes of the two voltages.
    if (!CalibrationReady(0)) {
      EndLevelCalibration(UI_MODE_ERROR);
      return;
    }
    const ChannelCalibrationData& c = settings_->calibration_data(
        CV_ADC_CHANNEL_V_OCT);
    for (int i = 0; i < 2; ++i) {
      calibration_note_[i] = c.Transform(calibration_reading_[i][0]);
    }
    calibration_v_oct_interval_ = calibration_interval_[0];
    mode_ = UI_MODE_CALIBRATION_C3;
    // Into LEVEL too (with a stackable cable or a mult): done.
    if (CalibrationReady(1)) {
      CalibrateC3();
    }
    return;
  }

  // Acquire offsets for all channels, except those which are patched (only
  // V/OCT should be).
  for (int i = 0; i < CV_ADC_CHANNEL_LAST; ++i) {
    bool patched = false;
    for (int j = 0; j < kNumNormalizedChannels; ++j) {
      patched = patched || (normalized_channels_[j] == i && \
          ((calibration_patched_ >> j) & 1));
    }
    if (i != CV_ADC_CHANNEL_V_OCT && !patched) {
      ChannelCalibrationData* c = settings_->mutable_calibration_data(i);
      c->offset = -cv_adc_.float_value(CvAdcChannel(i)) * c->scale;
    }
  }
  cv_c1_ = pitch_lp_calibration_;
  mode_ = UI_MODE_CALIBRATION_C3;
}

void Ui::CalibrateC3() {
  if (calibrating_level_) {
    // The same output is now patched into LEVEL, which reads the two
    // voltages, settled, as V/OCT did.
    const float level_interval = calibration_interval_[1];
    const float ratio = level_interval / calibration_v_oct_interval_;
    if (CalibrationReady(1) && ratio > kCalibrationMinRatio && \
        ratio < kCalibrationMaxRatio) {
      const float scale = \
          (calibration_note_[1] - calibration_note_[0]) / level_interval;
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

  if (delta > kVOctMinDelta && delta < kVOctMaxDelta) {
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
