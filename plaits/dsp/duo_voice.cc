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
// Duo mode voice.

#include "plaits/dsp/duo_voice.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

// Gain applied to each voice when the two voices are mixed together.
const float kDuoMixGain = 0.6f;

// Headroom for the models in which quadrature processing raises the peaks.
const float kQuadratureHeadroom = 0.6f;

void DuoVoice::Init(BufferAllocator* allocator) {
  engines_[0] = &virtual_analog_engine_;
  engines_[1] = &waveshaping_engine_;
  engines_[2] = &fm_engine_;
  engines_[3] = &grain_engine_;
  engines_[4] = &additive_engine_;
  engines_[5] = &wavetable_engine_;
  engines_[6] = &string_engine_;
  engines_[7] = &speech_engine_;

  // Split layout: same gains as the OUT signal of the original engines.
  // Stereo layout: the quadrature processing increases the peak level of FM
  // and wavetable signals (by up to 1.8x), which needs more headroom. The
  // string and speech models go through a limiter anyway.
  virtual_analog_engine_.set_post_processing_settings(false, 0.8f, 0.8f);
  waveshaping_engine_.set_post_processing_settings(false, 0.7f, 0.7f);
  fm_engine_.set_post_processing_settings(false, 0.6f, 0.6f * kQuadratureHeadroom);
  grain_engine_.set_post_processing_settings(false, 0.7f, 0.7f);
  additive_engine_.set_post_processing_settings(false, 0.8f, 0.8f);
  wavetable_engine_.set_post_processing_settings(false, 0.6f, 0.6f * kQuadratureHeadroom);
  string_engine_.set_post_processing_settings(true, -1.0f, -1.0f);
  speech_engine_.set_post_processing_settings(false, -0.7f, -0.7f);

  for (int i = 0; i < kNumDuoEngines; ++i) {
    // All engines share the same RAM space - also with the engines of the
    // original voice.
    allocator->Free();
    engines_[i]->Init(allocator);
  }

  engine_quantizer_.Init();
  previous_engine_index_ = -1;
  engine_cv_ = 0.0f;

  out_post_processor_.Init();
  aux_post_processor_.Init();

  decay_envelope_.Init();
  lpg_envelope_.Init();

  trigger_state_ = false;
  previous_note_[0] = previous_note_[1] = 0.0f;

  trigger_delay_.Init(trigger_delay_line_);
}

void DuoVoice::Render(
    const Patch& patch,
    const Modulations& modulations,
    Voice::Frame* frames,
    size_t size) {
  // Trigger, LPG, internal envelope.

  // Delay trigger by 1ms to deal with sequencers or MIDI interfaces whose
  // CV out lags behind the GATE out.
  trigger_delay_.Write(modulations.trigger);
  float trigger_value = trigger_delay_.Read(kTriggerDelay);

  bool previous_trigger_state = trigger_state_;
  if (!previous_trigger_state) {
    if (trigger_value > 0.3f) {
      trigger_state_ = true;
      // LEVEL is used as a pitch input: the LPG is always pinged by TRIGGER.
      lpg_envelope_.Trigger();
      decay_envelope_.Trigger();
      engine_cv_ = modulations.engine;
    }
  } else {
    if (trigger_value < 0.1f) {
      trigger_state_ = false;
    }
  }
  if (!modulations.trigger_patched) {
    engine_cv_ = modulations.engine;
  }

  // Model selection. The CV keeps the same volts-per-model as in the
  // original firmware (where it spans 16 models).
  int engine_index = engine_quantizer_.Process(
      patch.duo_engine & 7,
      engine_cv_ * 15.0f / 7.0f,
      kNumDuoEngines,
      0.25f);

  DuoEngine* e = engines_[engine_index];

  if (engine_index != previous_engine_index_) {
    e->Reset();
    out_post_processor_.Reset();
    aux_post_processor_.Reset();
    previous_engine_index_ = engine_index;
  }

  const bool stereo = !(patch.duo_engine & 8);
  const int num_voices = modulations.level_patched ? 2 : 1;

  EngineParameters p[kNumDuoVoices];

  bool rising_edge = trigger_state_ && !previous_trigger_state;
  float note = (modulations.note + previous_note_[0]) * 0.5f;
  float level_note = (modulations.level_note + previous_note_[1]) * 0.5f;
  previous_note_[0] = modulations.note;
  previous_note_[1] = modulations.level_note;
  const PostProcessingSettings& pp_s = e->post_processing_settings;

  if (modulations.trigger_patched) {
    p[0].trigger = rising_edge ? TRIGGER_RISING_EDGE : TRIGGER_LOW;
  } else {
    p[0].trigger = TRIGGER_UNPATCHED;
  }

  const float short_decay = (200.0f * kBlockSize) / kSampleRate *
      SemitonesToRatio(-96.0f * patch.decay);

  decay_envelope_.Process(short_decay * 2.0f);

  p[0].accent = 0.8f;

  bool use_internal_envelope = modulations.trigger_patched;

  // Actual synthesis parameters.

  p[0].harmonics = patch.harmonics + modulations.harmonics;
  CONSTRAIN(p[0].harmonics, 0.0f, 1.0f);

  float internal_envelope_amplitude = 1.0f;
  if (engine_index == kDuoSpeechEngineIndex) {
    internal_envelope_amplitude = 2.0f - p[0].harmonics * 6.0f;
    CONSTRAIN(internal_envelope_amplitude, 0.0f, 1.0f);
    speech_engine_.set_prosody_amount(
        !modulations.trigger_patched || modulations.frequency_patched ?
            0.0f : patch.frequency_modulation_amount);
    speech_engine_.set_speed(
        !modulations.trigger_patched || modulations.morph_patched ?
            0.0f : patch.morph_modulation_amount);
  }

  const float fm_envelope = internal_envelope_amplitude * \
      decay_envelope_.value() * decay_envelope_.value() * 48.0f;
  p[0].note = ApplyModulations(
      patch.note + note,
      patch.frequency_modulation_amount,
      modulations.frequency_patched,
      modulations.frequency,
      use_internal_envelope,
      fm_envelope,
      1.0f,
      -119.0f,
      120.0f);

  p[0].timbre = ApplyModulations(
      patch.timbre,
      patch.timbre_modulation_amount,
      modulations.timbre_patched,
      modulations.timbre,
      use_internal_envelope,
      decay_envelope_.value(),
      0.0f,
      0.0f,
      1.0f);

  p[0].morph = ApplyModulations(
      patch.morph,
      patch.morph_modulation_amount,
      modulations.morph_patched,
      modulations.morph,
      use_internal_envelope,
      internal_envelope_amplitude * decay_envelope_.value(),
      0.0f,
      0.0f,
      1.0f);

  // The second voice only differs by its pitch.
  p[1] = p[0];
  p[1].note = ApplyModulations(
      patch.note + level_note,
      patch.frequency_modulation_amount,
      modulations.frequency_patched,
      modulations.frequency,
      use_internal_envelope,
      fm_envelope,
      1.0f,
      -119.0f,
      120.0f);

  bool already_enveloped = pp_s.already_enveloped;
  e->Render(
      p, num_voices, stereo, out_buffer_, aux_buffer_, size,
      &already_enveloped);
  if (!stereo && num_voices == 1) {
    copy(&out_buffer_[0], &out_buffer_[size], &aux_buffer_[0]);
  }

  bool lpg_bypass = already_enveloped || !modulations.trigger_patched;

  // Compute LPG parameters.
  if (!lpg_bypass) {
    const float hf = patch.lpg_colour;
    const float decay_tail = (20.0f * kBlockSize) / kSampleRate *
        SemitonesToRatio(-72.0f * patch.decay + 12.0f * hf) - short_decay;
    const float attack = NoteToFrequency(p[0].note) * float(kBlockSize) * 2.0f;
    lpg_envelope_.ProcessPing(attack, short_decay, decay_tail, hf);
  }

  float gain = stereo ? pp_s.aux_gain : pp_s.out_gain;
  if (stereo && num_voices == 2) {
    gain *= kDuoMixGain;
  }

  out_post_processor_.Process(
      gain,
      lpg_bypass,
      lpg_envelope_.gain(),
      lpg_envelope_.frequency(),
      lpg_envelope_.hf_bleed(),
      out_buffer_,
      &frames->out,
      size,
      2);

  aux_post_processor_.Process(
      gain,
      lpg_bypass,
      lpg_envelope_.gain(),
      lpg_envelope_.frequency(),
      lpg_envelope_.hf_bleed(),
      aux_buffer_,
      &frames->aux,
      size,
      2);
}

}  // namespace plaits
