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
// Duo mode: two voices of the same model, the second one being pitched by
// the LEVEL input.
//
// - patch.duo_engine & 7 selects the model (classic, waveshaper, FM, formant,
//   harmonic, wavetable, string, vowel).
// - patch.duo_engine & 8 is clear for the stereo layout (green LEDs: MAIN and
//   AUX are the left and right channels of the mix of the two voices) and set
//   for the split layout (red LEDs: MAIN is the voice pitched by V/OCT, AUX
//   the voice pitched by LEVEL).
//
// Everything else behaves like the original voice, except that there is no
// LEVEL control anymore: the LPG is always pinged by TRIGGER, and the accent
// is fixed. When nothing is patched into LEVEL, the second voice is normalled
// to the first one: only one voice is synthesized and it is sent to both
// outputs (in stereo, or duplicated in the split layout).

#ifndef PLAITS_DSP_DUO_VOICE_H_
#define PLAITS_DSP_DUO_VOICE_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/hysteresis_quantizer.h"
#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/duo/duo_additive_engine.h"
#include "plaits/dsp/duo/duo_engine.h"
#include "plaits/dsp/duo/duo_fm_engine.h"
#include "plaits/dsp/duo/duo_grain_engine.h"
#include "plaits/dsp/duo/duo_speech_engine.h"
#include "plaits/dsp/duo/duo_string_engine.h"
#include "plaits/dsp/duo/duo_virtual_analog_engine.h"
#include "plaits/dsp/duo/duo_waveshaping_engine.h"
#include "plaits/dsp/duo/duo_wavetable_engine.h"
#include "plaits/dsp/voice.h"

namespace plaits {

const int kDuoSpeechEngineIndex = 7;

class DuoVoice {
 public:
  DuoVoice() { }
  ~DuoVoice() { }

  void Init(stmlib::BufferAllocator* allocator);
  void Render(
      const Patch& patch,
      const Modulations& modulations,
      Voice::Frame* frames,
      size_t size);

  // Forces the active engine to be reset on the next call to Render() -
  // required when coming back to duo mode, since all engines share the same
  // RAM.
  inline void ResetEngine() { previous_engine_index_ = -1; }

  inline int active_engine() const { return previous_engine_index_; }

 private:
  inline float ApplyModulations(
      float base_value,
      float modulation_amount,
      bool use_external_modulation,
      float external_modulation,
      bool use_internal_envelope,
      float envelope,
      float default_internal_modulation,
      float minimum_value,
      float maximum_value) {
    float value = base_value;
    modulation_amount *= std::max(fabsf(modulation_amount) - 0.05f, 0.05f);
    modulation_amount *= 1.05f;

    float modulation = use_external_modulation
        ? external_modulation
        : (use_internal_envelope ? envelope : default_internal_modulation);
    value += modulation_amount * modulation;
    CONSTRAIN(value, minimum_value, maximum_value);
    return value;
  }

  DuoVirtualAnalogEngine virtual_analog_engine_;
  DuoWaveshapingEngine waveshaping_engine_;
  DuoFMEngine fm_engine_;
  DuoGrainEngine grain_engine_;
  DuoAdditiveEngine additive_engine_;
  DuoWavetableEngine wavetable_engine_;
  DuoStringEngine string_engine_;
  DuoSpeechEngine speech_engine_;

  DuoEngine* engines_[kNumDuoEngines];

  stmlib::HysteresisQuantizer engine_quantizer_;

  int previous_engine_index_;
  float engine_cv_;

  float previous_note_[kNumDuoVoices];
  bool trigger_state_;

  DecayEnvelope decay_envelope_;
  LPGEnvelope lpg_envelope_;

  float trigger_delay_line_[kMaxTriggerDelay];
  DelayLine<float, kMaxTriggerDelay> trigger_delay_;

  ChannelPostProcessor out_post_processor_;
  ChannelPostProcessor aux_post_processor_;

  float out_buffer_[kMaxBlockSize];
  float aux_buffer_[kMaxBlockSize];

  DISALLOW_COPY_AND_ASSIGN(DuoVoice);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_VOICE_H_
