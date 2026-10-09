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
// Duo version of the 2-operator FM oscillator.
//
// Stereo: quadrature (see quadrature_stereo.h). Every partial of the FM
// spectrum appears in both channels with the same amplitude and with a phase
// difference of 90 degrees, so the magnitude spectra of the two channels and
// of their sum are the same as the original OUT. (Computing the rotated
// carrier directly in the 4x oversampled loop was tried, and was 4 times as
// expensive as rotating the downsampled mix.)

#ifndef PLAITS_DSP_DUO_DUO_FM_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_FM_ENGINE_H_

#include "plaits/dsp/duo/duo_engine.h"
#include "plaits/dsp/duo/quadrature_stereo.h"

namespace plaits {

class DuoFMVoice {
 public:
  DuoFMVoice() { }
  ~DuoFMVoice() { }

  void Init();

  void Render(const EngineParameters& parameters, float* out, size_t size);

 private:
  uint32_t carrier_phase_;
  uint32_t modulator_phase_;

  float previous_carrier_frequency_;
  float previous_modulator_frequency_;
  float previous_amount_;
  float previous_feedback_;
  float previous_sample_;

  float carrier_fir_;

  DISALLOW_COPY_AND_ASSIGN(DuoFMVoice);
};

class DuoFMEngine : public DuoEngine {
 public:
  DuoFMEngine() { }
  ~DuoFMEngine() { }

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
  DuoFMVoice voice_[kNumDuoVoices];
  QuadratureStereo quadrature_;

  DISALLOW_COPY_AND_ASSIGN(DuoFMEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_FM_ENGINE_H_
