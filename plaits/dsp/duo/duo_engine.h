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
// Base class for the engines used in duo mode. A duo engine renders two
// voices of the same model at once, so that the voices can share whatever
// does not depend on pitch (spectral envelopes, word banks, string pools...).
//
// Output conventions:
// - stereo == false: out receives voice 0 and aux receives voice 1. When
//   only one voice is rendered, aux is left untouched.
// - stereo == true: depends on the stereo layout of the engine. Unless the
//   engine makes its own stereo signal, the DuoVoice then turns the mix of
//   the voices into a stereo signal with one of the effects of
//   plaits/dsp/duo/duo_stereo.h.
//
// Only the OUT signal of the original engine is synthesized: the AUX
// variants of the original models are not used in duo mode, and the CPU
// time they would have used is spent on the second voice and on the stereo
// image instead.

#ifndef PLAITS_DSP_DUO_DUO_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_ENGINE_H_

#include "plaits/dsp/duo/duo_stereo.h"
#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kNumDuoVoices = 2;
const int kNumDuoEngines = 8;

enum DuoStereoLayout {
  // The engine renders the left and right channels in out and aux.
  DUO_STEREO_NATIVE,
  // The engine is never asked to render in stereo: the voices are rendered
  // as in the split layout, and mixed by the DuoVoice.
  DUO_STEREO_VOICES,
  // The engine renders the mix of the voices in out (and may use aux as a
  // scratch buffer), more efficiently than by rendering each voice.
  DUO_STEREO_MIX
};

class DuoEngine {
 public:
  DuoEngine() { }
  ~DuoEngine() { }
  virtual void Init(stmlib::BufferAllocator* allocator) = 0;
  virtual void Reset() = 0;
  virtual void Render(
      const EngineParameters* parameters,
      int num_voices,
      bool stereo,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped) = 0;

  PostProcessingSettings post_processing_settings;
  DuoStereoLayout stereo_layout;
  DuoStereoEffect stereo_effect;

  // In duo mode, out_gain is used for the split layout, and aux_gain for the
  // stereo layout (see DuoVoice::Init). The effect is not used with the
  // native layout.
  void set_post_processing_settings(
      bool already_enveloped,
      float split_gain,
      float stereo_gain,
      DuoStereoLayout layout,
      DuoStereoEffect effect) {
    post_processing_settings.already_enveloped = already_enveloped;
    post_processing_settings.out_gain = split_gain;
    post_processing_settings.aux_gain = stereo_gain;
    stereo_layout = layout;
    stereo_effect = effect;
  }
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_ENGINE_H_
