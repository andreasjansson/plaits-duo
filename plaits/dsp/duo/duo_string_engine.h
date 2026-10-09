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
// Duo version of the inharmonic string model.
//
// The original engine cycles through 3 strings, so that the previous notes
// keep ringing when a new note is triggered. Here, the two voices share the
// same pool of 3 strings (there is neither enough RAM nor enough CPU for 6):
// on each trigger, each voice moves to the string which has been ringing for
// the longest time. With a single voice, this is identical to the original
// engine. With two voices, each voice has an active string and one of the
// previous notes keeps ringing.
//
// Stereo: the three strings of the pool sit at different places in the
// stereo field (left, right, center), so that successive notes move across
// the field and their resonances overlap at different places, as on a harp.
// The result then goes through the quadrature stage (see
// quadrature_stereo.h), so that each string also has some width.
//
// The strings themselves are a faster implementation of the original string
// model (see duo_string.h): the original engine already uses 70% of the CPU
// with its three strings.
//
// Split outputs: each string is routed to the output of the voice which last
// triggered it.

#ifndef PLAITS_DSP_DUO_DUO_STRING_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_STRING_ENGINE_H_

#include "plaits/dsp/duo/duo_engine.h"
#include "plaits/dsp/duo/duo_string.h"
#include "plaits/dsp/duo/quadrature_stereo.h"
#include "plaits/dsp/physical_modelling/delay_line.h"

namespace plaits {

const int kDuoNumStrings = 3;

class DuoStringEngine : public DuoEngine {
 public:
  DuoStringEngine() { }
  ~DuoStringEngine() { }

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
  void Allocate(int num_voices);

  DuoStringVoice string_[kDuoNumStrings];
  float f0_[kDuoNumStrings];
  int owner_[kDuoNumStrings];
  uint32_t age_[kDuoNumStrings];
  bool newly_triggered_[kDuoNumStrings];

  int active_string_[kNumDuoVoices];
  DelayLine<float, 16> f0_delay_[kNumDuoVoices];
  uint32_t num_triggers_;
  
  QuadratureStereo quadrature_;

  float* temp_buffer_;
  float* string_buffer_;

  DISALLOW_COPY_AND_ASSIGN(DuoStringEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_STRING_ENGINE_H_
