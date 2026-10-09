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
// Naive speech synth, split for duo mode.

#include "plaits/dsp/duo/duo_naive_speech_synth.h"

#include <algorithm>

#include "stmlib/dsp/units.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void NaiveSpeechExcitation::Init() {
  pulse_.Init();
  click_duration_ = 0;
  pulse_coloration_.Init();
  pulse_coloration_.set_f_q<FREQUENCY_DIRTY>(800.0f / kSampleRate, 0.5f);
}

// Same as the first half of NaiveSpeechSynth::Render.
void NaiveSpeechExcitation::Render(
    bool click,
    float frequency,
    float* excitation,
    size_t size) {
  if (click) {
    click_duration_ = kSampleRate * 0.05f;
  }
  click_duration_ -= min(click_duration_, size);

  if (click_duration_) {
    frequency *= 0.5f;
  }

  // Generate excitation signal (glottal pulse).
  pulse_.Render<OSCILLATOR_SHAPE_IMPULSE_TRAIN>(
      frequency, 0.5f, excitation, size);
  pulse_coloration_.Process<FILTER_MODE_BAND_PASS>(
      excitation, excitation, size);
  for (size_t i = 0; i < size; ++i) {
    excitation[i] *= 4.0f;
  }
}

// Same as the second half of NaiveSpeechSynth::Render.
void NaiveSpeechFormants::Compute(
    float phoneme,
    float vocal_register,
    bool clicking) {
  typedef NaiveSpeechSynth::Formant Formant;
  
  float p = phoneme * (kNaiveSpeechNumPhonemes - 1.001f);
  float r = vocal_register * (kNaiveSpeechNumRegisters - 1.001f);
  
  MAKE_INTEGRAL_FRACTIONAL(p);
  MAKE_INTEGRAL_FRACTIONAL(r);
  
  for (int i = 0; i < kNaiveSpeechNumFormants; ++i) {
    const Formant& p0r0 = NaiveSpeechSynth::phonemes_[p_integral][r_integral].formant[i];
    const Formant& p0r1 = NaiveSpeechSynth::phonemes_[p_integral][r_integral + 1].formant[i];
    const Formant& p1r0 = NaiveSpeechSynth::phonemes_[p_integral + 1][r_integral].formant[i];
    const Formant& p1r1 = NaiveSpeechSynth::phonemes_[p_integral + 1][r_integral + 1].formant[i];

    float p0r_f = p0r0.frequency + \
        (p0r1.frequency - p0r0.frequency) * r_fractional;
    float p1r_f = p1r0.frequency + \
        (p1r1.frequency - p1r0.frequency) * r_fractional;
    float f = p0r_f + (p1r_f - p0r_f) * p_fractional;

    float p0r_a = p0r0.amplitude + \
        (p0r1.amplitude - p0r0.amplitude) * r_fractional;
    float p1r_a = p1r0.amplitude + \
        (p1r1.amplitude - p1r0.amplitude) * r_fractional;
    float a = (p0r_a + (p1r_a - p0r_a) * p_fractional) / 256.0f;
    
    if (f >= 160.0f) {
      f = 160.0f;
    }
    f = a0 * stmlib::SemitonesToRatio(f - 33.0f);
    if (clicking && i == 0) {
      f *= 0.5f;
    }
    
    // Same as Svf::set_f_q<FREQUENCY_DIRTY>(f, 20.0f).
    const float g = OnePole::tan<FREQUENCY_DIRTY>(f);
    const float r = 1.0f / 20.0f;
    const float h = 1.0f / (1.0f + r * g + g * g);
    r_plus_g[i] = r + g;
    g_times_h[i] = g * h;
    two_g[i] = 2.0f * g;
    amplitude[i] = a;
  }
}

void NaiveSpeechFormantBank::Init() {
  for (int i = 0; i < kNaiveSpeechNumFormants; ++i) {
    state_1_[i] = state_2_[i] = 0.0f;
  }
}

// Svf::ProcessAdd<FILTER_MODE_BAND_PASS> computes, for each sample:
//   hp = (in - r * s1 - g * s1 - s2) * h
//   bp = g * hp + s1
//   s1 = g * hp + bp
//   lp = g * bp + s2
//   s2 = g * bp + lp
//   out += amplitude * bp
// which is rearranged below, without computing hp and lp.
void NaiveSpeechFormantBank::Process(
    const NaiveSpeechFormants& formants,
    const float* excitation,
    float* out,
    size_t size) {
  float s1[kNaiveSpeechNumFormants];
  float s2[kNaiveSpeechNumFormants];
  for (int i = 0; i < kNaiveSpeechNumFormants; ++i) {
    s1[i] = state_1_[i];
    s2[i] = state_2_[i];
  }
  for (size_t j = 0; j < size; ++j) {
    const float in = excitation[j];
    float sum = 0.0f;
    for (int i = 0; i < kNaiveSpeechNumFormants; ++i) {
      const float g_hp = (in - formants.r_plus_g[i] * s1[i] - s2[i]) * \
          formants.g_times_h[i];
      const float bp = g_hp + s1[i];
      s1[i] = bp + g_hp;
      s2[i] += formants.two_g[i] * bp;
      sum += formants.amplitude[i] * bp;
    }
    out[j] = sum;
  }
  for (int i = 0; i < kNaiveSpeechNumFormants; ++i) {
    state_1_[i] = s1[i];
    state_2_[i] = s2[i];
  }
}

}  // namespace plaits
