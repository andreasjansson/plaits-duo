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
// NaiveSpeechSynth (see plaits/dsp/speech/naive_speech_synth.h), split in
// three parts so that two voices can share as much as possible:
//
// - NaiveSpeechExcitation: the glottal pulse (one per voice).
// - NaiveSpeechFormants: the frequencies and amplitudes of the 5 formants,
//   which depend on MORPH and TIMBRE but not on pitch (shared).
// - NaiveSpeechFormantBank: the 5 formant filters. Since the filters are
//   linear and their settings are shared, the sum of the excitations of the
//   two voices can go through a single bank when the voices are mixed.
//
// The formant filters are the same state variable filters as in the original
// synth, with the computations rearranged (see NaiveSpeechFormantBank) to
// take about half the time. They are not replaced by biquads with the same
// transfer function: these do not react as well as the SVF to the sudden
// changes of the first formant at the beginning and end of a click.

#ifndef PLAITS_DSP_DUO_DUO_NAIVE_SPEECH_SYNTH_H_
#define PLAITS_DSP_DUO_DUO_NAIVE_SPEECH_SYNTH_H_

#include "stmlib/dsp/filter.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/dsp/speech/naive_speech_synth.h"

namespace plaits {

class NaiveSpeechExcitation {
 public:
  NaiveSpeechExcitation() { }
  ~NaiveSpeechExcitation() { }

  void Init();
  void Render(bool click, float frequency, float* excitation, size_t size);

  inline bool clicking() const { return click_duration_ != 0; }

 private:
  Oscillator pulse_;
  size_t click_duration_;
  stmlib::Svf pulse_coloration_;

  DISALLOW_COPY_AND_ASSIGN(NaiveSpeechExcitation);
};

class NaiveSpeechFormants {
 public:
  NaiveSpeechFormants() { }
  ~NaiveSpeechFormants() { }

  void Compute(float phoneme, float vocal_register, bool clicking);

  // Settings of the band-pass SVF of each formant (see stmlib::Svf):
  // r + g, g * h and 2 * g, and amplitude of the formant.
  float r_plus_g[kNaiveSpeechNumFormants];
  float g_times_h[kNaiveSpeechNumFormants];
  float two_g[kNaiveSpeechNumFormants];
  float amplitude[kNaiveSpeechNumFormants];

 private:
  DISALLOW_COPY_AND_ASSIGN(NaiveSpeechFormants);
};

class NaiveSpeechFormantBank {
 public:
  NaiveSpeechFormantBank() { }
  ~NaiveSpeechFormantBank() { }

  void Init();

  // Writes the sum of the 5 formants to out.
  void Process(
      const NaiveSpeechFormants& formants,
      const float* excitation,
      float* out,
      size_t size);

 private:
  float state_1_[kNaiveSpeechNumFormants];
  float state_2_[kNaiveSpeechNumFormants];

  DISALLOW_COPY_AND_ASSIGN(NaiveSpeechFormantBank);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_NAIVE_SPEECH_SYNTH_H_
