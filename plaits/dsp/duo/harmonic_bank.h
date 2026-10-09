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
// The 24 harmonics of the harmonic oscillator model, as rendered by the two
// HarmonicOscillator<12> of AdditiveEngine (see
// plaits/dsp/oscillator/harmonic_oscillator.h), with two differences:
//
// - The harmonics are still computed in two batches of 12 (so that the
//   amplitudes stay in registers), but the second batch continues the
//   Chebyshev recursion of the first one, instead of starting it again from
//   two sine table lookups. Also, the second batch shares the phase of the
//   first one.
// - In stereo, the harmonics are split between two sides (see
//   kDuoHarmonicSideMask), except for the fundamental, which is centered.
//   Splitting costs nothing: each harmonic is still accumulated once.

#ifndef PLAITS_DSP_DUO_HARMONIC_BANK_H_
#define PLAITS_DSP_DUO_HARMONIC_BANK_H_

#include "stmlib/dsp/dsp.h"

#include "plaits/dsp/dsp.h"
#include "plaits/resources.h"

namespace plaits {

const int kDuoHarmonicBatchSize = 12;
const int kDuoNumHarmonics = 2 * kDuoHarmonicBatchSize;

// Bit i is set when harmonic i + 1 (or i + 13) goes to the second side:
// harmonics 3 4 | 7 8 | 11 12 | 15 16... on one side, 2 | 5 6 | 9 10... on
// the other, so that both sides get odd and even harmonics.
const uint32_t kDuoHarmonicSideMask = 0xccc;

// Amount of each side sent to the other one.
const float kDuoHarmonicCrossfeed = 0.3f;

class DuoHarmonicBank {
 public:
  DuoHarmonicBank() { }
  ~DuoHarmonicBank() { }

  void Init() {
    phase_ = 0.0f;
    frequency_ = 0.0f;
    for (int i = 0; i < kDuoNumHarmonics; ++i) {
      amplitude_[i] = 0.0f;
    }
  }

  // Mono: writes the sum of the harmonics to out.
  // Stereo: adds the left and right channels to out and aux (mirror swaps
  // the two sides).
  void Render(
      float frequency,
      const float* amplitudes,
      bool stereo,
      bool mirror,
      float* temp_a,
      float* temp_b,
      float* out,
      float* aux,
      size_t size) {
    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }

    float two_x[kMaxBlockSize];
    float previous[kMaxBlockSize];
    float current[kMaxBlockSize];

    // Phase and seeds of the Chebyshev recursion.
    const float frequency_increment = (frequency - frequency_) / float(size);
    float f = frequency_;
    float phase = phase_;
    for (size_t i = 0; i < size; ++i) {
      f += frequency_increment;
      phase += f;
      if (phase >= 1.0f) {
        phase -= 1.0f;
      }
      two_x[i] = 2.0f * stmlib::Interpolate(lut_sine, phase, 1024.0f);
      previous[i] = 1.0f;
      current[i] = two_x[i] * 0.5f;
    }
    phase_ = phase;
    frequency_ = f;

    const float one_over_size = 1.0f / float(size);
    if (stereo) {
      RenderBatch<1, true>(
          frequency, amplitudes, one_over_size, two_x, previous, current,
          temp_a, temp_b, size);
      RenderBatch<13, true>(
          frequency, amplitudes, one_over_size, two_x, previous, current,
          temp_a, temp_b, size);
      float* left = mirror ? aux : out;
      float* right = mirror ? out : aux;
      for (size_t i = 0; i < size; ++i) {
        const float a = temp_a[i];
        const float b = temp_b[i];
        left[i] += a + kDuoHarmonicCrossfeed * b;
        right[i] += b + kDuoHarmonicCrossfeed * a;
      }
    } else {
      RenderBatch<1, false>(
          frequency, amplitudes, one_over_size, two_x, previous, current,
          out, NULL, size);
      RenderBatch<13, false>(
          frequency, amplitudes, one_over_size, two_x, previous, current,
          out, NULL, size);
    }
  }

 private:
  template<int first_harmonic_index, bool split>
  void RenderBatch(
      float frequency,
      const float* amplitudes,
      float one_over_size,
      const float* two_x,
      float* previous,
      float* current,
      float* out_a,
      float* out_b,
      size_t size) {
    const int offset = first_harmonic_index - 1;
    float amplitude[kDuoHarmonicBatchSize];
    float increment[kDuoHarmonicBatchSize];
    for (int i = 0; i < kDuoHarmonicBatchSize; ++i) {
      float f = frequency * static_cast<float>(first_harmonic_index + i);
      if (f >= 0.5f) {
        f = 0.5f;
      }
      const float target = amplitudes[offset + i] * (1.0f - f * 2.0f);
      amplitude[i] = amplitude_[offset + i];
      increment[i] = (target - amplitude[i]) * one_over_size;
    }

    for (size_t j = 0; j < size; ++j) {
      const float t = two_x[j];
      float p = previous[j];
      float c = current[j];
      float sum_a = 0.0f;
      float sum_b = 0.0f;
      for (int i = 0; i < kDuoHarmonicBatchSize; ++i) {
        amplitude[i] += increment[i];
        if (!split) {
          sum_a += amplitude[i] * c;
        } else if (first_harmonic_index + i == 1) {
          const float s = 0.5f * amplitude[i] * c;
          sum_a += s;
          sum_b += s;
        } else if (kDuoHarmonicSideMask & (1 << i)) {
          sum_b += amplitude[i] * c;
        } else {
          sum_a += amplitude[i] * c;
        }
        const float temp = c;
        c = t * c - p;
        p = temp;
      }
      previous[j] = p;
      current[j] = c;
      if (first_harmonic_index == 1) {
        out_a[j] = sum_a;
        if (split) {
          out_b[j] = sum_b;
        }
      } else {
        out_a[j] += sum_a;
        if (split) {
          out_b[j] += sum_b;
        }
      }
    }

    for (int i = 0; i < kDuoHarmonicBatchSize; ++i) {
      amplitude_[offset + i] = amplitude[i];
    }
  }

  // Oscillator state.
  float phase_;

  // For interpolation of parameters.
  float frequency_;
  float amplitude_[kDuoNumHarmonics];

  DISALLOW_COPY_AND_ASSIGN(DuoHarmonicBank);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_HARMONIC_BANK_H_
