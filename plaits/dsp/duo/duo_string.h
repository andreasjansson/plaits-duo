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
// String and StringVoice (see plaits/dsp/physical_modelling/string.h and
// string_voice.h), rewritten for speed - the duo mode string model needs
// three of them, plus a stereo image and a second limiter, in the time the
// original engine used for its three strings.
//
// The algorithm is the same, and so is the output, up to floating point
// rounding. The differences are in the implementation:
// - All the state used in the per-sample loops (delay line write pointers,
//   filter states, the random number generator...) is held in local
//   variables. The original code keeps it in member variables, which the
//   compiler has to write back to memory at every sample since the output
//   buffers might alias them.
// - The state variable filters are computed with fewer operations (the
//   same rearrangement as in duo_naive_speech_synth.cc).
// - Divisions by values which are constant over a block are done once.

#ifndef PLAITS_DSP_DUO_DUO_STRING_H_
#define PLAITS_DSP_DUO_DUO_STRING_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/physical_modelling/string.h"

namespace plaits {

// Low-pass SVF, with the settings of stmlib::Svf.
class DuoLowPassSvf {
 public:
  DuoLowPassSvf() { }
  ~DuoLowPassSvf() { }

  // Same as Svf::Init.
  void Init() {
    set_g_r(stmlib::OnePole::tan<stmlib::FREQUENCY_DIRTY>(0.01f), 1.0f / 100.0f);
    state_1_ = state_2_ = 0.0f;
  }

  inline void set_g_r(float g, float r) {
    const float h = 1.0f / (1.0f + r * g + g * g);
    r_plus_g_ = r + g;
    g_times_h_ = g * h;
    g_ = g;
  }

  inline void Process(float* in_out, size_t size) {
    // The coefficients in local variables: the compiler would read them
    // again after each sample written (in_out could point to them).
    const float r_plus_g = r_plus_g_;
    const float g_times_h = g_times_h_;
    const float g = g_;
    float s1 = state_1_;
    float s2 = state_2_;
    for (size_t i = 0; i < size; ++i) {
      in_out[i] = Tick(in_out[i], r_plus_g, g_times_h, g, &s1, &s2);
    }
    state_1_ = s1;
    state_2_ = s2;
  }

  // Same as stmlib::Svf::Process<FILTER_MODE_LOW_PASS>, without computing
  // the high-pass output.
  static inline float Tick(
      float in,
      float r_plus_g,
      float g_times_h,
      float g,
      float* s1,
      float* s2) {
    const float g_hp = (in - r_plus_g * *s1 - *s2) * g_times_h;
    const float bp = g_hp + *s1;
    *s1 = bp + g_hp;
    const float g_bp = g * bp;
    const float lp = g_bp + *s2;
    *s2 = lp + g_bp;
    return lp;
  }

  float r_plus_g_;
  float g_times_h_;
  float g_;
  float state_1_;
  float state_2_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DuoLowPassSvf);
};

// The settings of a block which do not depend on the pitch, the same for all
// the strings: computed once (the same computations as in StringVoice::Render
// and String::ProcessInternal).
struct DuoStringParameters {
  void Compute(float accent, float structure, float brightness, float damping);

  float accent;
  // The excitation.
  float brightness;  // With the accent.
  float excitation_ratio;  // Of the cutoff of the filter to 4 * f0.
  float dust_f;
  float inv_dust_f;
  float dust_gain;
  // The string.
  bool curved_bridge;  // Otherwise: dispersion.
  float non_linearity_amount;
  float damping_ratio;  // Of the cutoff of the damping filter to f0.
  bool infinite_decay;
  float to_infinite;
  float damping_compensation;
  float stretch_point;
  float noise_amount;
  float noise_filter;
  float bridge_curving;
  float ap_gain;
  float stretch_factor;
};

class DuoString {
 public:
  DuoString() { }
  ~DuoString() { }

  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void Process(
      const DuoStringParameters& parameters,
      float f0,
      const float* in,
      float* out,
      size_t size);

 private:
  template<StringNonLinearity non_linearity>
  void ProcessInternal(
      const DuoStringParameters& parameters,
      float f0,
      const float* in,
      float* out,
      size_t size);

  float* string_;
  size_t string_write_ptr_;
  float* stretch_;
  size_t stretch_write_ptr_;

  DuoLowPassSvf iir_damping_filter_;
  float dc_blocker_x_;
  float dc_blocker_y_;

  float delay_;
  float dispersion_noise_;
  float curved_bridge_;

  float src_phase_;
  float out_sample_[2];

  DISALLOW_COPY_AND_ASSIGN(DuoString);
};

class DuoStringVoice {
 public:
  DuoStringVoice() { }
  ~DuoStringVoice() { }

  void Init(stmlib::BufferAllocator* allocator);
  void Reset();

  // Same as StringVoice::Render, without the exciter output.
  void Render(
      const DuoStringParameters& parameters,
      bool sustain,
      bool trigger,
      float f0,
      float* temp,
      float* out,
      size_t size);

 private:
  DuoLowPassSvf excitation_filter_;
  DuoString string_;
  size_t remaining_noise_samples_;

  DISALLOW_COPY_AND_ASSIGN(DuoStringVoice);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_STRING_H_
