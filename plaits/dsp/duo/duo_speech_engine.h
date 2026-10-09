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
// Duo version of the vowel and speech synthesis model.
//
// The two voices share the LPC word bank (the decoded words take 14kB of
// RAM, and decoding a bank is the most expensive thing this model does), but
// each voice has its own synthesizers and its own position in the bank.
//
// The naive formant synthesizer is split in parts (see
// duo_naive_speech_synth.h) so that the formant settings are computed once
// for both voices, and in stereo, the two voices share the same formant
// filters.


#ifndef PLAITS_DSP_DUO_DUO_SPEECH_ENGINE_H_
#define PLAITS_DSP_DUO_DUO_SPEECH_ENGINE_H_

#include "stmlib/dsp/hysteresis_quantizer.h"

#include "plaits/dsp/duo/duo_engine.h"
#include "plaits/dsp/duo/duo_naive_speech_synth.h"
#include "plaits/dsp/speech/lpc_speech_synth_controller.h"
#include "plaits/dsp/speech/sam_speech_synth.h"

namespace plaits {

class DuoSpeechEngine : public DuoEngine {
 public:
  DuoSpeechEngine() { }
  ~DuoSpeechEngine() { }

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

  inline void set_prosody_amount(float prosody_amount) {
    prosody_amount_ = prosody_amount;
  }

  inline void set_speed(float speed) {
    speed_ = speed;
  }

 private:
  stmlib::HysteresisQuantizer word_bank_quantizer_;

  NaiveSpeechExcitation naive_excitation_[kNumDuoVoices];
  NaiveSpeechFormants naive_formants_;
  NaiveSpeechFormantBank naive_formant_bank_[kNumDuoVoices];
  SAMSpeechSynth sam_speech_synth_[kNumDuoVoices];
  LPCSpeechSynthController lpc_speech_synth_controller_[kNumDuoVoices];
  LPCSpeechSynthWordBank lpc_speech_synth_word_bank_;
  int word_bank_seen_by_second_voice_;


  float* excitation_buffer_[2];
  float* temp_buffer_[2];
  float prosody_amount_;
  float speed_;

  DISALLOW_COPY_AND_ASSIGN(DuoSpeechEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_SPEECH_ENGINE_H_
