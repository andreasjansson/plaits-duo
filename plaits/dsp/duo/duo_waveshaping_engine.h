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
// Duo version of the waveshaping oscillator.
//
// Stereo: the waveshaped signal goes through two copies of the wavefolder,
// one driven slightly less hard than in the original engine and one slightly
// harder. Pitch and phase are shared by the two channels, but they fold at
// different points, and therefore have different harmonic structures - the
// difference growing with TIMBRE, as the signal is folded more and more.

#ifndef PLAITS_DSP_DUO_DUO_WAVESHAPING_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_WAVESHAPING_ENGINE_H_

#include "plaits/dsp/duo/duo_engine.h"
#include "plaits/dsp/oscillator/oscillator.h"

namespace plaits {

class DuoWaveshapingVoice {
 public:
  DuoWaveshapingVoice() { }
  ~DuoWaveshapingVoice() { }

  void Init();

  // When right is NULL, writes the mono signal to left. Otherwise, adds the
  // stereo signal to left and right.
  void Render(
      const EngineParameters& parameters,
      bool mirror,
      float* temp,
      float* left,
      float* right,
      size_t size);

 private:
  Oscillator slope_;
  float previous_shape_;
  float previous_wavefolder_gain_;

  DISALLOW_COPY_AND_ASSIGN(DuoWaveshapingVoice);
};

class DuoWaveshapingEngine : public DuoEngine {
 public:
  DuoWaveshapingEngine() { }
  ~DuoWaveshapingEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void Render(
      const EngineParameters* parameters,
      int num_voices,
      bool stereo,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

 private:
  DuoWaveshapingVoice voice_[kNumDuoVoices];
  float* temp_buffer_;

  DISALLOW_COPY_AND_ASSIGN(DuoWaveshapingEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_WAVESHAPING_ENGINE_H_
