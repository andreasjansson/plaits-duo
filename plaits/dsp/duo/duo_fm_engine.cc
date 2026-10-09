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
// Duo version of the 2-operator FM oscillator.

#include "plaits/dsp/duo/duo_fm_engine.h"

#include <algorithm>

#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const size_t kDuoFMOversampling = 4;
const size_t kDuoFMFirHalfSize = 4;

const float duo_fm_fir_coefficient[kDuoFMFirHalfSize] = {
    0.02442415f, 0.09297315f, 0.16712938f, 0.21547332f,
};

// Same as the Downsampler in fm_engine.cc.
class DuoFMDownsampler {
 public:
  DuoFMDownsampler(float* state) {
    head_ = *state;
    tail_ = 0.0f;
    state_ = state;
  }
  ~DuoFMDownsampler() {
    *state_ = head_;
  }
  inline void Accumulate(int i, float sample) {
    head_ += sample * duo_fm_fir_coefficient[3 - (i & 3)];
    tail_ += sample * duo_fm_fir_coefficient[i & 3];
  }

  inline float Read() {
    float value = head_;
    head_ = tail_;
    tail_ = 0.0f;
    return value;
  }
 private:
  float head_;
  float tail_;
  float* state_;

  DISALLOW_COPY_AND_ASSIGN(DuoFMDownsampler);
};

inline uint32_t PhaseModulate(uint32_t phase, float fm) {
  return phase + ((static_cast<uint32_t>((fm + 4.0f) * 536870912.0f)) << 3);
}

// Same as FMEngine::SinePM.
inline float DuoFMSine(uint32_t phase) {
  uint32_t integral = phase >> 22;
  float fractional = static_cast<float>(phase << 10) / 4294967296.0f;
  float a = lut_sine[integral];
  float b = lut_sine[integral + 1];
  return a + (b - a) * fractional;
}

}  // namespace

void DuoFMVoice::Init() {
  carrier_phase_ = 0;
  modulator_phase_ = 0;

  previous_carrier_frequency_ = a0;
  previous_modulator_frequency_ = a0;
  previous_amount_ = 0.0f;
  previous_feedback_ = 0.0f;
  previous_sample_ = 0.0f;

  carrier_fir_ = 0.0f;
}

void DuoFMVoice::Render(
    const EngineParameters& parameters,
    float* out,
    size_t size) {
  // 4x oversampling
  const float note = parameters.note - 24.0f;

  const float ratio = Interpolate(
      lut_fm_frequency_quantizer,
      parameters.harmonics,
      128.0f);

  float modulator_note = note + ratio;
  float target_modulator_frequency = NoteToFrequency(modulator_note);
  CONSTRAIN(target_modulator_frequency, 0.0f, 0.5f);

  // Reduce the maximum FM index for high pitched notes, to prevent aliasing.
  float hf_taming = 1.0f - (modulator_note - 72.0f) * 0.025f;
  CONSTRAIN(hf_taming, 0.0f, 1.0f);
  hf_taming *= hf_taming;

  ParameterInterpolator carrier_frequency(
      &previous_carrier_frequency_, NoteToFrequency(note), size);
  ParameterInterpolator modulator_frequency(
      &previous_modulator_frequency_, target_modulator_frequency, size);
  ParameterInterpolator amount_modulation(
      &previous_amount_,
      2.0f * parameters.timbre * parameters.timbre * hf_taming,
      size);
  ParameterInterpolator feedback_modulation(
      &previous_feedback_, 2.0f * parameters.morph - 1.0f, size);

  DuoFMDownsampler carrier_downsampler(&carrier_fir_);
  
  while (size--) {
    const float amount = amount_modulation.Next();
    const float feedback = feedback_modulation.Next();
    float phase_feedback = feedback < 0.0f ? 0.5f * feedback * feedback : 0.0f;
    const uint32_t carrier_increment = static_cast<uint32_t>(
        4294967296.0f * carrier_frequency.Next());
    float _modulator_frequency = modulator_frequency.Next();

    for (size_t j = 0; j < kDuoFMOversampling; ++j) {
      modulator_phase_ += static_cast<uint32_t>(4294967296.0f * \
           _modulator_frequency * (1.0f + previous_sample_ * phase_feedback));
      carrier_phase_ += carrier_increment;
      float modulator_fb = feedback > 0.0f ? 0.25f * feedback * feedback : 0.0f;
      float modulator = DuoFMSine(PhaseModulate(
          modulator_phase_, modulator_fb * previous_sample_));
      float carrier = DuoFMSine(PhaseModulate(
          carrier_phase_, amount * modulator));
      ONE_POLE(previous_sample_, carrier, 0.05f);
      carrier_downsampler.Accumulate(j, carrier);
    }
    *out++ = carrier_downsampler.Read();
  }
}

void DuoFMEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kNumDuoVoices; ++i) {
    voice_[i].Init();
  }
  quadrature_.Init();
}

void DuoFMEngine::Reset() {
  quadrature_.Init();
}

void DuoFMEngine::Render(
    const EngineParameters* parameters,
    int num_voices,
    bool stereo,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  for (int i = 0; i < num_voices; ++i) {
    voice_[i].Render(parameters[i], i == 0 ? out : aux, size);
  }
  if (stereo) {
    quadrature_.ProcessVoices(
        out, aux, num_voices, kDuoPanNear, kDuoPanFar, size);
  }
}

}  // namespace plaits
