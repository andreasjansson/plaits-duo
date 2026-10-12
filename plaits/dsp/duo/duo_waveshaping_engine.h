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
// -----------------------------------------------------------------------------
//
// Duo version of the waveshaping oscillator. Each voice is a
// WaveshapingEngine, rendering OUT only.
//
// Stereo: the waveshaped signal goes through two wavefolders, offset in
// opposite directions (by an amount which slowly breathes), one for each
// channel. For a waveform with half-wave symmetry, the two channels then
// have the same harmonic levels: the odd harmonics are in phase, the even
// ones in opposite phase.

#ifndef PLAITS_DSP_DUO_DUO_WAVESHAPING_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_WAVESHAPING_ENGINE_H_

#include "plaits/dsp/duo/duo_lfo.h"
#include "plaits/dsp/duo/duo_original_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"

namespace plaits {

class DuoWaveshapingEngine : public DuoOriginalEngine<WaveshapingEngine> {
 public:
  DuoWaveshapingEngine() { }
  ~DuoWaveshapingEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator) {
    DuoOriginalEngine<WaveshapingEngine>::Init(allocator);
    temp_buffer_[0] = allocator->Allocate<float>(kMaxBlockSize);
    temp_buffer_[1] = allocator->Allocate<float>(kMaxBlockSize);
    for (int i = 0; i < kNumDuoVoices; ++i) {
      breath_[i].Init(0.13f, 0.16f * i);
    }
  }

  virtual void Render(
      const EngineParameters* parameters,
      int num_voices,
      bool stereo,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped) {
    if (!stereo) {
      // OUT of the original engine, without its AUX signal.
      for (int i = 0; i < num_voices; ++i) {
        voice_[i].Render(
            parameters[i], i == 0 ? out : aux, NULL, size, already_enveloped);
      }
      return;
    }
    for (int i = 0; i < num_voices; ++i) {
      const float offset = 0.035f + 0.02f * breath_[i].Next(size);
      voice_[i].RenderStereo(
          parameters[i],
          offset,
          i == 0 ? out : temp_buffer_[0],
          i == 0 ? aux : temp_buffer_[1],
          size);
    }
    if (num_voices == 2) {
      for (size_t j = 0; j < size; ++j) {
        out[j] += temp_buffer_[0][j];
        aux[j] += temp_buffer_[1][j];
      }
    }
  }

 private:
  DuoLfo breath_[kNumDuoVoices];
  float* temp_buffer_[2];

  DISALLOW_COPY_AND_ASSIGN(DuoWaveshapingEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_WAVESHAPING_ENGINE_H_
