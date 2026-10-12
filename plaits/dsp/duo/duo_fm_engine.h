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
// The 2-operator FM model of the original firmware for two voices, with the
// sine table in RAM (its 4 times oversampled loop reads it about 400 times
// per block).

#ifndef PLAITS_DSP_DUO_DUO_FM_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_FM_ENGINE_H_

#include "plaits/dsp/duo/duo_original_engine.h"
#include "plaits/dsp/duo/duo_ram_table.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/resources.h"

namespace plaits {

class DuoFMEngine : public DuoOriginalEngine<FMEngine> {
 public:
  DuoFMEngine() { }
  ~DuoFMEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator) {
    DuoOriginalEngine<FMEngine>::Init(allocator);
    // FMEngine::SinePM reads up to lut_sine[1024].
    sine_.Init(allocator, lut_sine, 1025);
  }

  virtual void Reset() {
    DuoOriginalEngine<FMEngine>::Reset();
    sine_.Reset();
  }

  virtual void Render(
      const EngineParameters* parameters,
      int num_voices,
      bool stereo,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped) {
    const float* sine = sine_.Update();
    for (int i = 0; i < kNumDuoVoices; ++i) {
      voice_[i].set_sine_table(sine);
    }
    DuoOriginalEngine<FMEngine>::Render(
        parameters, num_voices, stereo, out, aux, size, already_enveloped);
  }

 private:
  DuoRamTable<float> sine_;

  DISALLOW_COPY_AND_ASSIGN(DuoFMEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_FM_ENGINE_H_
