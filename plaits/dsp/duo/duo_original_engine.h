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
// Duo version of the models of the original firmware whose two voices have
// nothing to share (classic waveforms, waveshaper, 2-op FM and formant): two
// instances of the original engine, rendering OUT only (the engines skip AUX
// when it is NULL).

#ifndef PLAITS_DSP_DUO_DUO_ORIGINAL_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_ORIGINAL_ENGINE_H_

#include "plaits/dsp/duo/duo_engine.h"

namespace plaits {

template<typename Engine>
class DuoOriginalEngine : public DuoEngine {
 public:
  DuoOriginalEngine() { }
  ~DuoOriginalEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator) {
    for (int i = 0; i < kNumDuoVoices; ++i) {
      voice_[i].Init(allocator);
    }
  }

  virtual void Reset() {
    for (int i = 0; i < kNumDuoVoices; ++i) {
      voice_[i].Reset();
    }
  }

  // Rendered as in the split layout (see DUO_STEREO_VOICES).
  virtual void Render(
      const EngineParameters* parameters,
      int num_voices,
      bool stereo,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped) {
    for (int i = 0; i < num_voices; ++i) {
      voice_[i].Render(
          parameters[i], i == 0 ? out : aux, NULL, size, already_enveloped);
    }
  }

 protected:
  Engine voice_[kNumDuoVoices];

  DISALLOW_COPY_AND_ASSIGN(DuoOriginalEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_ORIGINAL_ENGINE_H_
