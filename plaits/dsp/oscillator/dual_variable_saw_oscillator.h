// Copyright 2016 Emilie Gillet.
// Copyright 2026 Andreas Jansson.
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
// Two variable saw oscillators (see variable_saw_oscillator.h) with the same
// pulse width and waveshape, at different frequencies, rendered in the same
// loop - which shares the parameter interpolation and the computation of the
// slopes. Used for the two channels of the classic waveforms in stereo.

#ifndef PLAITS_DSP_OSCILLATOR_DUAL_VARIABLE_SAW_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_DUAL_VARIABLE_SAW_OSCILLATOR_H_

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/dsp/oscillator/variable_saw_oscillator.h"

namespace plaits {

class DualVariableSawOscillator {
 public:
  DualVariableSawOscillator() { }
  ~DualVariableSawOscillator() { }

  void Init() {
    for (int i = 0; i < 2; ++i) {
      state_[i].phase = 0.0f;
      state_[i].next_sample = 0.0f;
      state_[i].high = false;
      frequency_[i] = 0.01f;
    }
    previous_pw_ = 0.5f;
    pw_ = 0.5f;
    waveshape_ = 0.0f;
  }

  void Render(
      float frequency_a,
      float frequency_b,
      float pw,
      float waveshape,
      float* out_a,
      float* out_b,
      size_t size) {
    if (frequency_a >= kMaxFrequency) {
      frequency_a = kMaxFrequency;
    }
    if (frequency_b >= kMaxFrequency) {
      frequency_b = kMaxFrequency;
    }
    // The pulse width constraint of the higher frequency applies to both.
    const float frequency = std::max(frequency_a, frequency_b);
    if (frequency >= 0.25f) {
      pw = 0.5f;
    } else {
      CONSTRAIN(pw, frequency * 2.0f, 1.0f - 2.0f * frequency);
    }

    stmlib::ParameterInterpolator fm_a(&frequency_[0], frequency_a, size);
    stmlib::ParameterInterpolator fm_b(&frequency_[1], frequency_b, size);
    stmlib::ParameterInterpolator pwm(&pw_, pw, size);
    stmlib::ParameterInterpolator waveshape_modulation(
        &waveshape_, waveshape, size);

    // The state is kept in local variables through the block.
    State a = state_[0];
    State b = state_[1];
    float previous_pw = previous_pw_;
    while (size--) {
      const float pw = pwm.Next();
      const float waveshape = waveshape_modulation.Next();
      const float triangle_amount = waveshape;
      const float notch_amount = 1.0f - waveshape;
      const float slope_up = 1.0f / (pw);
      const float slope_down = 1.0f / (1.0f - pw);
      *out_a++ = Tick(
          &a, fm_a.Next(), pw, previous_pw, slope_up, slope_down,
          triangle_amount, notch_amount);
      *out_b++ = Tick(
          &b, fm_b.Next(), pw, previous_pw, slope_up, slope_down,
          triangle_amount, notch_amount);
      previous_pw = pw;
    }
    state_[0] = a;
    state_[1] = b;
    previous_pw_ = previous_pw;
  }

 private:
  struct State {
    float phase;
    float next_sample;
    bool high;
  };

  // One iteration of VariableSawOscillator::Render.
  static inline __attribute__((always_inline)) float Tick(
      State* state,
      float frequency,
      float pw,
      float previous_pw,
      float slope_up,
      float slope_down,
      float triangle_amount,
      float notch_amount) {
    float this_sample = state->next_sample;
    float next_sample = 0.0f;
    float phase = state->phase + frequency;

    if (!state->high && phase >= pw) {
      const float triangle_step = (slope_up + slope_down) * frequency * \
          triangle_amount;
      const float notch = (kVariableSawNotchDepth + 1.0f - pw) * notch_amount;
      const float t = (phase - pw) / (previous_pw - pw + frequency);
      this_sample += notch * stmlib::ThisBlepSample(t);
      next_sample += notch * stmlib::NextBlepSample(t);
      this_sample -= triangle_step * stmlib::ThisIntegratedBlepSample(t);
      next_sample -= triangle_step * stmlib::NextIntegratedBlepSample(t);
      state->high = true;
    } else if (phase >= 1.0f) {
      phase -= 1.0f;
      const float triangle_step = (slope_up + slope_down) * frequency * \
          triangle_amount;
      const float notch = (kVariableSawNotchDepth + 1.0f) * notch_amount;
      const float t = phase / frequency;
      this_sample -= notch * stmlib::ThisBlepSample(t);
      next_sample -= notch * stmlib::NextBlepSample(t);
      this_sample += triangle_step * stmlib::ThisIntegratedBlepSample(t);
      next_sample += triangle_step * stmlib::NextIntegratedBlepSample(t);
      state->high = false;
    }

    const float notch_saw = phase < pw ? phase : 1.0f + kVariableSawNotchDepth;
    const float triangle = phase < pw
        ? phase * slope_up
        : 1.0f - (phase - pw) * slope_down;
    next_sample += notch_saw * notch_amount + triangle * triangle_amount;
    state->phase = phase;
    state->next_sample = next_sample;
    // A multiplication instead of the division of the original.
    const float scale = 1.0f / (1.0f + kVariableSawNotchDepth);
    return (2.0f * this_sample - 1.0f) * scale;
  }

  // Oscillator state.
  State state_[2];
  float previous_pw_;

  // For interpolation of parameters.
  float frequency_[2];
  float pw_;
  float waveshape_;

  DISALLOW_COPY_AND_ASSIGN(DualVariableSawOscillator);
};

}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_DUAL_VARIABLE_SAW_OSCILLATOR_H_
