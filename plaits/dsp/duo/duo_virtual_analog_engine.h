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
// Duo version of the classic waveforms. Each voice is a VirtualAnalogEngine,
// rendering OUT only.
//
// Stereo: the variable square is the same in both channels, and each
// channel has its own variable saw. The two saws are detuned in opposite
// directions by 3 to 9 cents (slowly drifting), so that the beating between
// the oscillators moves between the two channels.

#ifndef PLAITS_DSP_DUO_DUO_VIRTUAL_ANALOG_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_VIRTUAL_ANALOG_ENGINE_H_

#include "stmlib/dsp/units.h"

#include "plaits/dsp/duo/duo_lfo.h"
#include "plaits/dsp/duo/duo_original_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"

namespace plaits {

class DuoVirtualAnalogEngine : public DuoOriginalEngine<VirtualAnalogEngine> {
 public:
  DuoVirtualAnalogEngine() { }
  ~DuoVirtualAnalogEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator) {
    DuoOriginalEngine<VirtualAnalogEngine>::Init(allocator);
    temp_buffer_[0] = allocator->Allocate<float>(kMaxBlockSize);
    temp_buffer_[1] = allocator->Allocate<float>(kMaxBlockSize);
    for (int i = 0; i < kNumDuoVoices; ++i) {
      drift_[i][0].Init(0.1f, 0.16f * i);
      drift_[i][1].Init(0.037f, 0.32f * i);
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
      DuoOriginalEngine<VirtualAnalogEngine>::Render(
          parameters, num_voices, stereo, out, aux, size, already_enveloped);
      return;
    }
    for (int i = 0; i < num_voices; ++i) {
      const float cents = 6.0f + 3.0f * (
          0.6f * drift_[i][0].Next(size) + 0.4f * drift_[i][1].Next(size));
      voice_[i].RenderStereo(
          parameters[i],
          stmlib::SemitonesToRatio(0.005f * cents),
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
  DuoLfo drift_[kNumDuoVoices][2];
  float* temp_buffer_[2];

  DISALLOW_COPY_AND_ASSIGN(DuoVirtualAnalogEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_VIRTUAL_ANALOG_ENGINE_H_
