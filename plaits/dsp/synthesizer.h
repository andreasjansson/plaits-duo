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
// The original voice and the duo mode voice, one of which is rendered
// depending on patch.duo_mode. Used by the firmware and by the host
// simulator.

#ifndef PLAITS_DSP_SYNTHESIZER_H_
#define PLAITS_DSP_SYNTHESIZER_H_

#include "stmlib/stmlib.h"
#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/duo_voice.h"
#include "plaits/dsp/voice.h"

namespace plaits {

class Synthesizer {
 public:
  Synthesizer() { }
  ~Synthesizer() { }

  void Init(stmlib::BufferAllocator* allocator) {
    voice_.Init(allocator);
    duo_voice_.Init(allocator);
    duo_mode_ = false;
  }

  void Render(
      const Patch& patch,
      const Modulations& modulations,
      Voice::Frame* frames,
      size_t size) {
    if (patch.duo_mode != duo_mode_) {
      // The engines of the two voices share the same RAM: the engine of the
      // voice we are switching to has to be reset.
      if (patch.duo_mode) {
        duo_voice_.ResetEngine();
      } else {
        voice_.ResetEngine();
      }
      duo_mode_ = patch.duo_mode;
    }
    if (duo_mode_) {
      duo_voice_.Render(patch, modulations, frames, size);
    } else {
      voice_.Render(patch, modulations, frames, size);
    }
  }

  // The model being played, for the LEDs: 0 to 7 for the green LEDs, 8 to
  // 15 for the red LEDs.
  inline int active_engine(const Patch& patch) const {
    return duo_mode_
        ? duo_voice_.active_engine() | (patch.duo_engine & 8)
        : voice_.active_engine();
  }

 private:
  Voice voice_;
  DuoVoice duo_voice_;
  bool duo_mode_;

  DISALLOW_COPY_AND_ASSIGN(Synthesizer);
};

}  // namespace plaits

#endif  // PLAITS_DSP_SYNTHESIZER_H_
