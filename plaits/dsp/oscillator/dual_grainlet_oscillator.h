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
// Two GrainletOscillators (see plaits/dsp/oscillator/grainlet_oscillator.h)
// sharing the same carrier frequency, shape and bleed - which is how the
// formant model uses them. Since the two carriers always have the same phase,
// the carrier waveform is computed only once. The outputs are bit-identical
// to those of two GrainletOscillators.

#ifndef PLAITS_DSP_OSCILLATOR_DUAL_GRAINLET_OSCILLATOR_H_
#define PLAITS_DSP_OSCILLATOR_DUAL_GRAINLET_OSCILLATOR_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/resources.h"

namespace plaits {

class DualGrainletOscillator {
 public:
  DualGrainletOscillator() { }
  ~DualGrainletOscillator() { }

  void Init() {
    carrier_phase_ = 0.0f;
    formant_phase_[0] = formant_phase_[1] = 0.0f;
    next_sample_[0] = next_sample_[1] = 0.0f;

    carrier_frequency_ = 0.0f;
    formant_frequency_[0] = formant_frequency_[1] = 0.0f;
    carrier_shape_ = 0.0f;
    carrier_bleed_ = 0.0f;
  }

  void Render(
      float carrier_frequency,
      float formant_frequency_a,
      float formant_frequency_b,
      float carrier_shape,
      float carrier_bleed,
      float* out_a,
      float* out_b,
      size_t size) {
    if (carrier_frequency >= kMaxFrequency * 0.5f) {
      carrier_frequency = kMaxFrequency * 0.5f;
    }
    if (formant_frequency_a >= kMaxFrequency) {
      formant_frequency_a = kMaxFrequency;
    }
    if (formant_frequency_b >= kMaxFrequency) {
      formant_frequency_b = kMaxFrequency;
    }

    stmlib::ParameterInterpolator carrier_frequency_modulation(
        &carrier_frequency_,
        carrier_frequency,
        size);
    stmlib::ParameterInterpolator formant_frequency_a_modulation(
        &formant_frequency_[0],
        formant_frequency_a,
        size);
    stmlib::ParameterInterpolator formant_frequency_b_modulation(
        &formant_frequency_[1],
        formant_frequency_b,
        size);
    stmlib::ParameterInterpolator carrier_shape_modulation(
        &carrier_shape_,
        carrier_shape,
        size);
    stmlib::ParameterInterpolator carrier_bleed_modulation(
        &carrier_bleed_,
        carrier_bleed,
        size);

    float next_sample_a = next_sample_[0];
    float next_sample_b = next_sample_[1];
    float formant_phase_a = formant_phase_[0];
    float formant_phase_b = formant_phase_[1];

    while (size--) {
      float this_sample_a = next_sample_a;
      float this_sample_b = next_sample_b;
      next_sample_a = 0.0f;
      next_sample_b = 0.0f;

      const float f0 = carrier_frequency_modulation.Next();
      const float f1_a = formant_frequency_a_modulation.Next();
      const float f1_b = formant_frequency_b_modulation.Next();

      carrier_phase_ += f0;

      if (carrier_phase_ >= 1.0f) {
        carrier_phase_ -= 1.0f;
        const float reset_time = carrier_phase_ / f0;

        const float shape_before = carrier_shape_modulation.subsample(
            1.0f - reset_time);
        const float bleed_before = carrier_bleed_modulation.subsample(
            1.0f - reset_time);
        const float carrier_before = Carrier(1.0f, shape_before);
        const float before_a = Grain(
            carrier_before,
            Sine(formant_phase_a + (1.0f - reset_time) * f1_a),
            bleed_before);
        const float before_b = Grain(
            carrier_before,
            Sine(formant_phase_b + (1.0f - reset_time) * f1_b),
            bleed_before);

        const float bleed_after = carrier_bleed_modulation.subsample(1.0f);
        const float after = Grain(
            Carrier(0.0f, carrier_shape_modulation.subsample(1.0f)),
            Sine(0.0f),
            bleed_after);

        const float this_blep = stmlib::ThisBlepSample(reset_time);
        const float next_blep = stmlib::NextBlepSample(reset_time);
        this_sample_a += (after - before_a) * this_blep;
        next_sample_a += (after - before_a) * next_blep;
        this_sample_b += (after - before_b) * this_blep;
        next_sample_b += (after - before_b) * next_blep;
        formant_phase_a = reset_time * f1_a;
        formant_phase_b = reset_time * f1_b;
      } else {
        formant_phase_a += f1_a;
        if (formant_phase_a >= 1.0f) {
          formant_phase_a -= 1.0f;
        }
        formant_phase_b += f1_b;
        if (formant_phase_b >= 1.0f) {
          formant_phase_b -= 1.0f;
        }
      }

      const float shape = carrier_shape_modulation.Next();
      const float bleed = carrier_bleed_modulation.Next();
      const float carrier = Carrier(carrier_phase_, shape);
      next_sample_a += Grain(carrier, Sine(formant_phase_a), bleed);
      next_sample_b += Grain(carrier, Sine(formant_phase_b), bleed);
      *out_a++ = this_sample_a;
      *out_b++ = this_sample_b;
    }

    next_sample_[0] = next_sample_a;
    next_sample_[1] = next_sample_b;
    formant_phase_[0] = formant_phase_a;
    formant_phase_[1] = formant_phase_b;
  }

 private:
  static inline float Sine(float phase) {
    return stmlib::InterpolateWrap(lut_sine, phase, 1024.0f);
  }

  // Same as GrainletOscillator::Carrier.
  static inline float Carrier(float phase, float shape) {
    shape *= 3.0f;
    MAKE_INTEGRAL_FRACTIONAL(shape);
    float t = 1.0f - shape_fractional;

    if (shape_integral == 0) {
      phase = phase * (1.0f + t * t * t * 15.0f);
      if (phase >= 1.0f) {
        phase = 1.0f;
      }
      phase += 0.75f;
    } else if (shape_integral == 1) {
      float breakpoint = 0.001f + 0.499f * t * t * t;
      if (phase < breakpoint) {
        phase *= (0.5f / breakpoint);
      } else {
        phase = 0.5f + (phase - breakpoint) * 0.5f / (1.0f - breakpoint);
      }
      phase += 0.75f;
    } else {
      t = 1.0f - t;
      phase = 0.25f + phase * (0.5f + t * t * t * 14.5f);
      if (phase >= 0.75f) phase = 0.75f;
    }
    return (Sine(phase) + 1.0f) * 0.25f;
  }

  // Same as GrainletOscillator::Grainlet, with the carrier computed by the
  // caller.
  static inline float Grain(float carrier, float formant, float bleed) {
    return carrier * (formant + bleed) / (1.0f + bleed);
  }

  // Oscillator state.
  float carrier_phase_;
  float formant_phase_[2];
  float next_sample_[2];

  // For interpolation of parameters.
  float carrier_frequency_;
  float formant_frequency_[2];
  float carrier_shape_;
  float carrier_bleed_;

  DISALLOW_COPY_AND_ASSIGN(DualGrainletOscillator);
};

}  // namespace plaits

#endif  // PLAITS_DSP_OSCILLATOR_DUAL_GRAINLET_OSCILLATOR_H_
