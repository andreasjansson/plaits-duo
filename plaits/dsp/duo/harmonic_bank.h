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
// plaits/dsp/oscillator/harmonic_oscillator.h). The harmonics are still
// computed in two batches of 12 (so that the amplitudes stay in registers),
// but the second batch continues the Chebyshev recursion of the first one,
// instead of starting it again from two sine table lookups. Also, the second
// batch shares the phase of the first one.
//
// Stereo: each harmonic has its own phase difference between the two
// channels. With x = cos(theta), cos(k theta) = T_k(x) and sin(k theta) =
// sin(theta) U_{k-1}(x), where T and U are the Chebyshev polynomials of the
// first and second kinds. Harmonic k is advanced in phase by an angle psi_k
// in the left channel, and delayed by the same angle in the right channel:
// cos(k theta +- psi_k) = cos(psi_k) cos(k theta) -+ sin(psi_k) sin(k theta).
// The left and right channels are thus mid -+ sin(theta) side, where mid
// and side are sums of the U_k(x) (since T_k = (U_k - U_{k-2}) / 2), which
// are all given by a single recursion: 3 multiply-adds per harmonic and per
// sample, instead of 2 for a mono signal. The amplitudes are kept constant
// through the block.

#ifndef PLAITS_DSP_DUO_HARMONIC_BANK_H_
#define PLAITS_DSP_DUO_HARMONIC_BANK_H_

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"

#include "plaits/dsp/dsp.h"
#include "plaits/resources.h"

namespace plaits {

const int kDuoHarmonicBatchSize = 12;
const int kDuoHarmonicStereoBatchSize = 12;
const int kDuoNumHarmonics = 2 * kDuoHarmonicBatchSize;

class DuoHarmonicBank {
 public:
  DuoHarmonicBank() { }
  ~DuoHarmonicBank() { }

  void Init() {
    phase_ = 0.0f;
    frequency_ = 0.0f;
    sine_ = lut_sine;
    for (int i = 0; i < kDuoNumHarmonics; ++i) {
      amplitude_[i] = 0.0f;
    }
  }

  // The sine table (lut_sine, or its copy in RAM).
  inline void set_sine_table(const float* sine) { sine_ = sine; }

  // Writes the sum of the harmonics to out.
  void Render(
      float frequency,
      const float* amplitudes,
      float* out,
      size_t size) {
    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }

    float two_x[kMaxBlockSize];
    float previous[kMaxBlockSize];
    float current[kMaxBlockSize];
    ComputePhase(frequency, two_x, NULL, size);
    for (size_t i = 0; i < size; ++i) {
      previous[i] = 1.0f;
      current[i] = two_x[i] * 0.5f;
    }

    const float one_over_size = 1.0f / float(size);
    std::fill(&out[0], &out[size], 0.0f);
    for (int batch = 0; batch < 2; ++batch) {
      RenderBatch(
          1 + batch * kDuoHarmonicBatchSize,
          frequency, amplitudes, one_over_size, two_x, previous, current,
          out, size);
    }
  }

  // Writes (or adds, if accumulate is set) the left and right channels to
  // left and right. In the left channel, harmonic k + 1 is advanced in phase
  // by an angle psi_k, and in the right channel it is delayed by the same
  // angle; rotation_cos[k] and rotation_sin[k] are cos(psi_k) / 4 and
  // sin(psi_k) / 2 (scaled to save a few multiplications).
  void RenderStereo(
      float frequency,
      const float* amplitudes,
      const float* rotation_cos,
      const float* rotation_sin,
      bool accumulate,
      float* left,
      float* right,
      size_t size) {
    if (frequency >= 0.5f) {
      frequency = 0.5f;
    }

    float two_x[kMaxBlockSize];
    float sine[kMaxBlockSize];
    ComputePhase(frequency, two_x, sine, size);

    // Coefficients of U_j in the mid and side sums, for j = 0 to
    // kDuoNumHarmonics. T_k = (U_k - U_{k-2}) / 2, with U_{-1} = 0, so that
    // the coefficient of U_j in the mid sum is (m_j - m_{j+2}) / 2, where m_k
    // is the coefficient of T_k (and m_0 = 0).
    // (mid[-1] receives a coefficient which is not used.) With p_k the sum
    // of the amplitudes of harmonic k at the start and at the end of the
    // block, m_k = p_k cos(psi_k) / 2, and mid[j] = (p_j rotation_cos[j - 1]
    // - p_{j+2} rotation_cos[j + 1]).
    float mid_buffer[kDuoNumHarmonics + 2];
    float* mid = &mid_buffer[1];
    float side[kDuoNumHarmonics + 1];
    float q_2 = 0.0f;  // m_{k-2} / 2
    float q_1 = 0.0f;  // m_{k-1} / 2
    float attenuation = 1.0f;
    const float attenuation_step = 2.0f * frequency;
    for (int k = 1; k <= kDuoNumHarmonics; k += 2) {
      // Two harmonics at a time.
      const float* a = &amplitudes[k - 1];
      const float* c = &rotation_cos[k - 1];
      const float* s = &rotation_sin[k - 1];
      float* previous_amplitude = &amplitude_[k - 1];

      // 1 - 2f, or 0 above the Nyquist frequency.
      attenuation -= attenuation_step;
      float target = a[0] * 0.5f * (attenuation + fabsf(attenuation));
      float p = previous_amplitude[0] + target;
      previous_amplitude[0] = target;
      const float q = p * c[0];
      // sin(k theta) = sin(theta) U_{k-1}.
      side[k - 1] = p * s[0];
      mid[k - 2] = q_2 - q;

      attenuation -= attenuation_step;
      target = a[1] * 0.5f * (attenuation + fabsf(attenuation));
      p = previous_amplitude[1] + target;
      previous_amplitude[1] = target;
      const float q_next = p * c[1];
      side[k] = p * s[1];
      mid[k - 1] = q_1 - q_next;

      q_2 = q;
      q_1 = q_next;
    }
    mid[kDuoNumHarmonics - 1] = q_2;
    mid[kDuoNumHarmonics] = q_1;
    side[kDuoNumHarmonics] = 0.0f;

    float previous[kMaxBlockSize];
    float current[kMaxBlockSize];
    float mid_sum[kMaxBlockSize];
    float side_sum[kMaxBlockSize];
    const int b = kDuoHarmonicStereoBatchSize;
    RenderStereoBatch<true, false>(
        1, mid, side, two_x, sine, previous, current, mid_sum, side_sum,
        false, left, right, size);
    for (int j = 1 + b; j < kDuoNumHarmonics + 1 - b; j += b) {
      RenderStereoBatch<false, false>(
          j, mid, side, two_x, sine, previous, current, mid_sum, side_sum,
          false, left, right, size);
    }
    RenderStereoBatch<false, true>(
        kDuoNumHarmonics + 1 - b, mid, side, two_x, sine, previous, current,
        mid_sum, side_sum, accumulate, left, right, size);
  }

 private:
  // Phase, and x = sin(2 pi phase) = cos(theta), with theta = 2 pi phase -
  // pi / 2. two_x receives 2x, and sine (if not NULL) receives sin(theta) =
  // -cos(2 pi phase).
  void ComputePhase(
      float frequency,
      float* two_x,
      float* sine,
      size_t size) {
    const float frequency_increment = (frequency - frequency_) / float(size);
    float f = frequency_;
    float phase = phase_;
    for (size_t i = 0; i < size; ++i) {
      f += frequency_increment;
      phase += f;
      if (phase >= 1.0f) {
        phase -= 1.0f;
      }
      // Same as stmlib::Interpolate. The cosine (a quarter of a cycle later
      // in the table) is interpolated at the same position.
      const float index = phase * 1024.0f;
      MAKE_INTEGRAL_FRACTIONAL(index)
      const float* table = &sine_[index_integral];
      two_x[i] = 2.0f * (table[0] + (table[1] - table[0]) * index_fractional);
      if (sine) {
        sine[i] = -(table[256] + (table[257] - table[256]) * index_fractional);
      }
    }
    phase_ = phase;
    frequency_ = f;
  }

  // Adds the harmonics first_harmonic_index to first_harmonic_index + 11 to
  // out.
  void RenderBatch(
      int first_harmonic_index,
      float frequency,
      const float* amplitudes,
      float one_over_size,
      const float* two_x,
      float* previous,
      float* current,
      float* out,
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
      float sum = 0.0f;
      for (int i = 0; i < kDuoHarmonicBatchSize; ++i) {
        amplitude[i] += increment[i];
        sum += amplitude[i] * c;
        const float temp = c;
        c = t * c - p;
        p = temp;
      }
      previous[j] = p;
      current[j] = c;
      out[j] += sum;
    }

    for (int i = 0; i < kDuoHarmonicBatchSize; ++i) {
      amplitude_[offset + i] = amplitude[i];
    }
  }

  // Adds the terms of U_first_index to
  // U_{first_index + kDuoHarmonicStereoBatchSize - 1} to the mid and side
  // sums. The first batch starts the recursion, the last one writes (or
  // adds) the left and right channels.
  template<bool first, bool last>
  void RenderStereoBatch(
      int first_index,
      const float* mid,
      const float* side,
      const float* two_x,
      const float* sine,
      float* previous,
      float* current,
      float* mid_sum,
      float* side_sum,
      bool accumulate,
      float* left,
      float* right,
      size_t size) {
    const int n = kDuoHarmonicStereoBatchSize;
    float mid_coefficient[n];
    float side_coefficient[n];
    for (int i = 0; i < n; ++i) {
      mid_coefficient[i] = mid[first_index + i];
      side_coefficient[i] = side[first_index + i];
    }
    const float mid_0 = mid[0];
    const float side_0 = side[0];

    for (size_t j = 0; j < size; ++j) {
      const float t = two_x[j];
      // U_{-1} = 0, U_0 = 1.
      float p = first ? 0.0f : previous[j];
      float c = first ? 1.0f : current[j];
      float m = first ? mid_0 : mid_sum[j];
      float s = first ? side_0 : side_sum[j];
      for (int i = 0; i < n; ++i) {
        float product = t * c;
#ifndef TEST
        // A multiplication and a subtraction (2 cycles) rather than a fused
        // multiply-subtract (3 cycles): this loop is limited by the time
        // spent in the FPU rather than by the instruction fetches.
        __asm ("" : "+t" (product));
#endif  // TEST
        const float u = product - p;
        m += mid_coefficient[i] * u;
        s += side_coefficient[i] * u;
        p = c;
        c = u;
      }
      if (last) {
        const float l = m - sine[j] * s;
        const float r = m + sine[j] * s;
        left[j] = accumulate ? left[j] + l : l;
        right[j] = accumulate ? right[j] + r : r;
      } else {
        previous[j] = p;
        current[j] = c;
        mid_sum[j] = m;
        side_sum[j] = s;
      }
    }
  }

  // Oscillator state.
  const float* sine_;
  float phase_;

  // For interpolation of parameters.
  float frequency_;
  float amplitude_[kDuoNumHarmonics];

  DISALLOW_COPY_AND_ASSIGN(DuoHarmonicBank);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_HARMONIC_BANK_H_
