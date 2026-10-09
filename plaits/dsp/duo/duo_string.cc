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
// String model for duo mode.

#include "plaits/dsp/duo/duo_string.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/dsp.h"
#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

const size_t kDuoStringMask = kDelayLineSize - 1;
const size_t kDuoStretchMask = kDelayLineSize / 4 - 1;

namespace {

// Same as Random::GetFloat, on a local copy of the state.
inline float DuoRandomFloat(uint32_t* state) {
  *state = *state * 1664525L + 1013904223L;
  return static_cast<float>(*state) / 4294967296.0f;
}

}  // namespace

void DuoString::Init(BufferAllocator* allocator) {
  string_ = allocator->Allocate<float>(kDelayLineSize);
  stretch_ = allocator->Allocate<float>(kDelayLineSize / 4);
  delay_ = 100.0f;
  Reset();
}

// Same as String::Reset.
void DuoString::Reset() {
  fill(&string_[0], &string_[kDelayLineSize], 0.0f);
  fill(&stretch_[0], &stretch_[kDelayLineSize / 4], 0.0f);
  string_write_ptr_ = 0;
  stretch_write_ptr_ = 0;
  iir_damping_filter_.Init();
  dc_blocker_x_ = 0.0f;
  dc_blocker_y_ = 0.0f;
  dispersion_noise_ = 0.0f;
  curved_bridge_ = 0.0f;
  out_sample_[0] = out_sample_[1] = 0.0f;
  src_phase_ = 0.0f;
}

void DuoString::Process(
    float f0,
    float non_linearity_amount,
    float brightness,
    float damping,
    const float* in,
    float* out,
    size_t size) {
  if (non_linearity_amount <= 0.0f) {
    ProcessInternal<STRING_NON_LINEARITY_CURVED_BRIDGE>(
        f0, -non_linearity_amount, brightness, damping, in, out, size);
  } else {
    ProcessInternal<STRING_NON_LINEARITY_DISPERSION>(
        f0, non_linearity_amount, brightness, damping, in, out, size);
  }
}

// Same as String::ProcessInternal.
template<StringNonLinearity non_linearity>
void DuoString::ProcessInternal(
    float f0,
    float non_linearity_amount,
    float brightness,
    float damping,
    const float* in,
    float* out,
    size_t size) {
  float delay = 1.0f / f0;
  CONSTRAIN(delay, 4.0f, kDelayLineSize - 4.0f);

  // If there is not enough delay time in the delay line, we play at the
  // lowest possible note and we upsample on the fly with a shitty linear
  // interpolator. We don't care because it's a corner case (f0 < 11.7Hz)
  float src_ratio = delay * f0;
  if (src_ratio >= 0.9999f) {
    // When we are above 11.7 Hz, we make sure that the linear interpolator
    // does not get in the way.
    src_phase_ = 1.0f;
    src_ratio = 1.0f;
  }

  float damping_cutoff = min(
      12.0f + damping * damping * 60.0f + brightness * 24.0f,
      84.0f);
  float damping_f = min(f0 * SemitonesToRatio(damping_cutoff), 0.499f);

  // Crossfade to infinite decay.
  if (damping >= 0.95f) {
    float to_infinite = 20.0f * (damping - 0.95f);
    brightness += to_infinite * (1.0f - brightness);
    damping_f += to_infinite * (0.4999f - damping_f);
    damping_cutoff += to_infinite * (128.0f - damping_cutoff);
  }

  // Same as iir_damping_filter_.set_f_q<FREQUENCY_FAST>(damping_f, 0.5f).
  iir_damping_filter_.set_g_r(OnePole::tan<FREQUENCY_FAST>(damping_f), 2.0f);

  float damping_compensation = Interpolate(lut_svf_shift, damping_cutoff, 1.0f);

  // Linearly interpolate delay time.
  ParameterInterpolator delay_modulation(
      &delay_, delay * damping_compensation, size);

  float stretch_point = non_linearity_amount * (2.0f - non_linearity_amount) * 0.225f;
  float stretch_correction = (160.0f / kSampleRate) * delay;
  CONSTRAIN(stretch_correction, 1.0f, 2.1f);

  float noise_amount_sqrt = non_linearity_amount > 0.75f
      ? 4.0f * (non_linearity_amount - 0.75f)
      : 0.0f;
  float noise_amount = noise_amount_sqrt * noise_amount_sqrt * 0.1f;
  float noise_filter = 0.06f + 0.94f * brightness * brightness;

  float bridge_curving_sqrt = non_linearity_amount;
  float bridge_curving = bridge_curving_sqrt * bridge_curving_sqrt * 0.01f;

  float ap_gain = -0.618f * non_linearity_amount / (0.15f + fabsf(non_linearity_amount));
  const float stretch_factor = 0.408f - stretch_point * 0.308f;

  // State.
  float* const string = string_;
  float* const stretch = stretch_;
  size_t string_write_ptr = string_write_ptr_;
  size_t stretch_write_ptr = stretch_write_ptr_;
  float damping_s1 = iir_damping_filter_.state_1_;
  float damping_s2 = iir_damping_filter_.state_2_;
  const float dc_blocker_pole = 1.0f - 20.0f / kSampleRate;
  float dc_blocker_x = dc_blocker_x_;
  float dc_blocker_y = dc_blocker_y_;
  float dispersion_noise = dispersion_noise_;
  float curved_bridge = curved_bridge_;
  float src_phase = src_phase_;
  float out_sample_0 = out_sample_[0];
  float out_sample_1 = out_sample_[1];
  uint32_t random_state = Random::state();

  while (size--) {
    src_phase += src_ratio;
    if (src_phase > 1.0f) {
      src_phase -= 1.0f;

      float delay = delay_modulation.Next();
      float s = 0.0f;

      if (non_linearity == STRING_NON_LINEARITY_DISPERSION) {
        float noise = DuoRandomFloat(&random_state) - 0.5f;
        ONE_POLE(dispersion_noise, noise, noise_filter)
        delay *= 1.0f + dispersion_noise * noise_amount;
      } else {
        delay *= 1.0f - curved_bridge * bridge_curving;
      }

      bool hermite = true;
      if (non_linearity == STRING_NON_LINEARITY_DISPERSION) {
        float ap_delay = delay * stretch_point;
        float main_delay = delay - ap_delay * stretch_factor * stretch_correction;
        if (ap_delay >= 4.0f && main_delay >= 4.0f) {
          hermite = false;
          // DelayLine::Read.
          MAKE_INTEGRAL_FRACTIONAL(main_delay)
          const float a = string[(string_write_ptr + main_delay_integral) & \
              kDuoStringMask];
          const float b = string[(string_write_ptr + main_delay_integral + 1) & \
              kDuoStringMask];
          s = a + (b - a) * main_delay_fractional;
          // DelayLine::Allpass.
          const size_t ap_delay_integral = static_cast<size_t>(ap_delay);
          const float read = stretch[(stretch_write_ptr + ap_delay_integral) & \
              kDuoStretchMask];
          const float write = s + ap_gain * read;
          stretch[stretch_write_ptr] = write;
          stretch_write_ptr = (stretch_write_ptr - 1) & kDuoStretchMask;
          s = -write * ap_gain + read;
        }
      }
      if (hermite) {
        // DelayLine::ReadHermite.
        MAKE_INTEGRAL_FRACTIONAL(delay)
        const size_t t = string_write_ptr + delay_integral + kDelayLineSize;
        const float xm1 = string[(t - 1) & kDuoStringMask];
        const float x0 = string[t & kDuoStringMask];
        const float x1 = string[(t + 1) & kDuoStringMask];
        const float x2 = string[(t + 2) & kDuoStringMask];
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float b_neg = w + a;
        const float f = delay_fractional;
        s = (((a * f) - b_neg) * f + c) * f + x0;
      }

      if (non_linearity == STRING_NON_LINEARITY_CURVED_BRIDGE) {
        float value = fabsf(s) - 0.025f;
        float sign = s > 0.0f ? 1.0f : -1.5f;
        curved_bridge = (fabsf(value) + value) * sign;
      }

      s += *in;
      CONSTRAIN(s, -20.0f, +20.0f);

      // DCBlocker::Process.
      const float old_x = dc_blocker_x;
      dc_blocker_x = s;
      s = dc_blocker_y = dc_blocker_y * dc_blocker_pole + s - old_x;

      s = iir_damping_filter_.Tick(s, &damping_s1, &damping_s2);

      // DelayLine::Write.
      string[string_write_ptr] = s;
      string_write_ptr = (string_write_ptr - 1) & kDuoStringMask;

      out_sample_1 = out_sample_0;
      out_sample_0 = s;
    }
    *out++ += Crossfade(out_sample_1, out_sample_0, src_phase);
    in++;
  }

  string_write_ptr_ = string_write_ptr;
  stretch_write_ptr_ = stretch_write_ptr;
  iir_damping_filter_.state_1_ = damping_s1;
  iir_damping_filter_.state_2_ = damping_s2;
  dc_blocker_x_ = dc_blocker_x;
  dc_blocker_y_ = dc_blocker_y;
  dispersion_noise_ = dispersion_noise;
  curved_bridge_ = curved_bridge;
  src_phase_ = src_phase;
  out_sample_[0] = out_sample_0;
  out_sample_[1] = out_sample_1;
  Random::Seed(random_state);
}

void DuoStringVoice::Init(BufferAllocator* allocator) {
  excitation_filter_.Init();
  string_.Init(allocator);
  remaining_noise_samples_ = 0;
}

void DuoStringVoice::Reset() {
  string_.Reset();
}

void DuoStringVoice::Render(
    bool sustain,
    bool trigger,
    float accent,
    float f0,
    float structure,
    float brightness,
    float damping,
    float* temp,
    float* out,
    size_t size) {
  const float density = brightness * brightness;

  brightness += 0.25f * accent * (1.0f - brightness);
  damping += 0.25f * accent * (1.0f - damping);

  // Synthesize excitation signal.
  if (trigger || sustain) {
    const float range = 72.0f;
    const float f = 4.0f * f0;
    const float cutoff = min(
        f * SemitonesToRatio((brightness * (2.0f - brightness) - 0.5f) * range),
        0.499f);
    const float q = sustain ? 1.0f : 0.5f;
    remaining_noise_samples_ = static_cast<size_t>(1.0f / f0);
    // Same as excitation_filter_.set_f_q<FREQUENCY_DIRTY>(cutoff, q).
    excitation_filter_.set_g_r(OnePole::tan<FREQUENCY_DIRTY>(cutoff), 1.0f / q);
  }

  if (sustain) {
    // Same as Dust(), with the division done once.
    const float dust_f = 0.00005f + 0.99995f * density * density;
    const float inv_dust_f = 1.0f / dust_f;
    const float dust_gain = 8.0f - dust_f * 6.0f;
    uint32_t random_state = Random::state();
    for (size_t i = 0; i < size; ++i) {
      const float u = DuoRandomFloat(&random_state);
      temp[i] = (u < dust_f ? u * inv_dust_f : 0.0f) * dust_gain * accent;
    }
    Random::Seed(random_state);
  } else if (remaining_noise_samples_) {
    size_t noise_samples = min(remaining_noise_samples_, size);
    remaining_noise_samples_ -= noise_samples;
    size_t tail = size - noise_samples;
    float* start = temp;
    while (noise_samples--) {
      *start++ = 2.0f * Random::GetFloat() - 1.0f;
    }
    while (tail--) {
      *start++ = 0.0f;
    }
  } else {
    fill(&temp[0], &temp[size], 0.0f);
  }

  excitation_filter_.Process(temp, size);

  float non_linearity = structure < 0.24f
      ? (structure - 0.24f) * 4.166f
      : (structure > 0.26f ? (structure - 0.26f) * 1.35135f : 0.0f);
  string_.Process(f0, non_linearity, brightness, damping, temp, out, size);
}

}  // namespace plaits
