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

#include "plaits/dsp/duo/duo_speech_engine.h"

#include <algorithm>

#include "plaits/dsp/speech/lpc_speech_synth_words.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void DuoSpeechEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kNumDuoVoices; ++i) {
    sam_speech_synth_[i].Init();
    naive_excitation_[i].Init();
    naive_formant_bank_[i].Init();
  }
  lpc_speech_synth_word_bank_.Init(
      word_banks_,
      LPC_SPEECH_SYNTH_NUM_WORD_BANKS,
      allocator);
  for (int i = 0; i < kNumDuoVoices; ++i) {
    lpc_speech_synth_controller_[i].Init(&lpc_speech_synth_word_bank_);
  }
  word_bank_quantizer_.Init();
  word_bank_seen_by_second_voice_ = -1;

  excitation_buffer_[0] = allocator->Allocate<float>(kMaxBlockSize);
  excitation_buffer_[1] = allocator->Allocate<float>(kMaxBlockSize);
  temp_buffer_[0] = allocator->Allocate<float>(kMaxBlockSize);
  temp_buffer_[1] = allocator->Allocate<float>(kMaxBlockSize);

  prosody_amount_ = 0.0f;
  speed_ = 1.0f;
}

void DuoSpeechEngine::Reset() {
  lpc_speech_synth_word_bank_.Reset();
  word_bank_seen_by_second_voice_ = -1;
}

void DuoSpeechEngine::Render(
    const EngineParameters* parameters,
    int num_voices,
    bool stereo,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  // Same as SpeechEngine::Render, for each voice. The excitation signal
  // (originally sent to AUX) goes to a scratch buffer. The output of the
  // second voice is rendered in aux, and in stereo, added to out.
  const EngineParameters& p = parameters[0];
  float* voice_out[kNumDuoVoices] = { out, aux };
  const float group = p.harmonics * 6.0f;
  bool voices_mixed = false;

  if (group <= 2.0f) {
    *already_enveloped = false;

    float blend = group;
    if (group <= 1.0f) {
      // The formant filter settings do not depend on pitch, and the voices
      // receive the same triggers, so they are clicking at the same time.
      for (int v = 0; v < num_voices; ++v) {
        naive_excitation_[v].Render(
            parameters[v].trigger == TRIGGER_RISING_EDGE,
            NoteToFrequency(parameters[v].note),
            excitation_buffer_[v],
            size);
      }
      naive_formants_.Compute(
          p.morph, p.timbre, naive_excitation_[0].clicking());
      if (stereo) {
        // The filters being linear, the two voices can be filtered at once.
        if (num_voices == 2) {
          for (size_t i = 0; i < size; ++i) {
            excitation_buffer_[0][i] += excitation_buffer_[1][i];
          }
        }
        naive_formant_bank_[0].Process(
            naive_formants_, excitation_buffer_[0], out, size);
      } else {
        for (int v = 0; v < num_voices; ++v) {
          naive_formant_bank_[v].Process(
              naive_formants_, excitation_buffer_[v], voice_out[v], size);
        }
      }
    } else {
      for (int v = 0; v < num_voices; ++v) {
        lpc_speech_synth_controller_[v].Render(
            parameters[v].trigger & TRIGGER_UNPATCHED,
            parameters[v].trigger & TRIGGER_RISING_EDGE,
            -1,
            NoteToFrequency(parameters[v].note),
            0.0f,
            0.0f,
            p.morph,
            p.timbre,
            1.0f,
            excitation_buffer_[0],
            voice_out[v],
            size);
      }
      if (stereo && num_voices == 2) {
        for (size_t i = 0; i < size; ++i) {
          out[i] += aux[i];
        }
      }
      blend = 2.0f - blend;
    }
    voices_mixed = stereo;

    for (int v = 0; v < num_voices; ++v) {
      sam_speech_synth_[v].Render(
          parameters[v].trigger == TRIGGER_RISING_EDGE,
          NoteToFrequency(parameters[v].note),
          p.morph,
          p.timbre,
          temp_buffer_[0],
          v == 0 ? temp_buffer_[1] : excitation_buffer_[1],
          size);
    }
    if (stereo && num_voices == 2) {
      for (size_t i = 0; i < size; ++i) {
        temp_buffer_[1][i] += excitation_buffer_[1][i];
      }
    }

    blend *= blend * (3.0f - 2.0f * blend);
    blend *= blend * (3.0f - 2.0f * blend);
    if (stereo) {
      for (size_t i = 0; i < size; ++i) {
        out[i] += (temp_buffer_[1][i] - out[i]) * blend;
      }
    } else {
      for (int v = 0; v < num_voices; ++v) {
        const float* sam = v == 0 ? temp_buffer_[1] : excitation_buffer_[1];
        float* destination = voice_out[v];
        for (size_t i = 0; i < size; ++i) {
          destination[i] += (sam[i] - destination[i]) * blend;
        }
      }
    }
  } else {
    // Change phonemes/words for LPC.
    const int word_bank = word_bank_quantizer_.Process(
        (group - 2.0f) * 0.275f,
        LPC_SPEECH_SYNTH_NUM_WORD_BANKS + 1) - 1;

    const bool replay_prosody = word_bank >= 0 && \
        !(p.trigger & TRIGGER_UNPATCHED);

    *already_enveloped = replay_prosody;

    for (int v = 0; v < num_voices; ++v) {
      if (v == 1 && word_bank >= 0 && \
          word_bank != word_bank_seen_by_second_voice_) {
        // The first voice has loaded a different bank, from which the second
        // voice can't continue playing.
        lpc_speech_synth_controller_[1].ResetPlayback();
        word_bank_seen_by_second_voice_ = word_bank;
      }
      lpc_speech_synth_controller_[v].Render(
          parameters[v].trigger & TRIGGER_UNPATCHED,
          parameters[v].trigger & TRIGGER_RISING_EDGE,
          word_bank,
          NoteToFrequency(parameters[v].note),
          prosody_amount_,
          speed_,
          p.morph,
          p.timbre,
          replay_prosody ? p.accent : 1.0f,
          excitation_buffer_[0],
          voice_out[v],
          size);
    }
  }

  if (num_voices == 1) {
    // The second voice will have to start over when it is enabled again.
    word_bank_seen_by_second_voice_ = -1;
  }

  if (stereo && num_voices == 2 && !voices_mixed) {
    for (size_t i = 0; i < size; ++i) {
      out[i] += aux[i];
    }
  }
}

}  // namespace plaits
