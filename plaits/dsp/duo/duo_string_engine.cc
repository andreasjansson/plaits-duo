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

#include "plaits/dsp/duo/duo_string_engine.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

// Position of the strings in the stereo field.
static const float string_pan[kDuoNumStrings][2] = {
  { 0.9510565f, 0.3090170f },
  { 0.3090170f, 0.9510565f },
  { 0.70710678f, 0.70710678f },
};

void DuoStringEngine::Init(BufferAllocator* allocator) {
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize);
  string_buffer_ = allocator->Allocate<float>(kMaxBlockSize);
  for (int i = 0; i < kDuoNumStrings; ++i) {
    string_[i].Init(allocator);
  }
  for (int i = 0; i < kNumDuoVoices; ++i) {
    f0_delay_[i].Init(allocator->Allocate<float>(16));
  }
  Reset();
}

void DuoStringEngine::Reset() {
  for (int i = 0; i < kDuoNumStrings; ++i) {
    string_[i].Reset();
    f0_[i] = 0.01f;
    owner_[i] = 0;
    age_[i] = 0;
    newly_triggered_[i] = false;
  }
  for (int i = 0; i < kNumDuoVoices; ++i) {
    f0_delay_[i].Reset();
  }
  // Same initial string as the original engine for the first voice.
  active_string_[0] = kDuoNumStrings - 1;
  active_string_[1] = kDuoNumStrings - 2;
  owner_[active_string_[1]] = 1;
  num_triggers_ = 0;
  quadrature_.Init();
}

void DuoStringEngine::Allocate(int num_voices) {
  ++num_triggers_;
  // With two voices, alternate the voice that gets to pick first, so that
  // the two voices take turns in leaving a note ringing.
  const int first = num_voices == 2 ? (num_triggers_ & 1) : 0;
  for (int n = 0; n < num_voices; ++n) {
    const int v = (first + n) % num_voices;
    // The released string keeps the pitch it had slightly before the trigger
    // (see StringEngine::Render).
    f0_[active_string_[v]] = f0_delay_[v].Read(14);

    int best = -1;
    for (int i = 0; i < kDuoNumStrings; ++i) {
      if (i == active_string_[v]) {
        continue;
      }
      if (num_voices == 2 && i == active_string_[1 - v]) {
        continue;
      }
      if (best == -1 || age_[i] < age_[best]) {
        best = i;
      }
    }
    active_string_[v] = best;
    owner_[best] = v;
    age_[best] = num_triggers_ * 2 + n;
    newly_triggered_[best] = true;
  }
}

void DuoStringEngine::Render(
    const EngineParameters* parameters,
    int num_voices,
    bool stereo,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const EngineParameters& p = parameters[0];

  if (num_voices == 2 && active_string_[1] == active_string_[0]) {
    // The second voice has just been enabled: give it its own string.
    active_string_[1] = (active_string_[0] + 1) % kDuoNumStrings;
    owner_[active_string_[1]] = 1;
  }

  if (p.trigger & TRIGGER_RISING_EDGE) {
    Allocate(num_voices);
  }

  for (int v = 0; v < kNumDuoVoices; ++v) {
    // When the second voice is disabled, its pitch history follows the
    // first voice, so that it is valid as soon as it is enabled.
    const float f0 = NoteToFrequency(parameters[min(v, num_voices - 1)].note);
    if (v < num_voices) {
      f0_[active_string_[v]] = f0;
    }
    f0_delay_[v].Write(f0);
  }

  if (!stereo) {
    // The strings are directly added to the output of their voice.
    fill(&out[0], &out[size], 0.0f);
    if (num_voices == 2) {
      fill(&aux[0], &aux[size], 0.0f);
    }
  } else {
    fill(&string_buffer_[0], &string_buffer_[size], 0.0f);
  }

  for (int i = 0; i < kDuoNumStrings; ++i) {
    bool active = i == active_string_[0] || \
        (num_voices == 2 && i == active_string_[1]);
    float* destination = stereo
        ? string_buffer_
        : ((num_voices == 2 && owner_[i] == 1) ? aux : out);
    string_[i].Render(
        (p.trigger & TRIGGER_UNPATCHED) && active,
        (p.trigger & TRIGGER_RISING_EDGE) && newly_triggered_[i],
        p.accent,
        f0_[i],
        p.harmonics,
        p.timbre * p.timbre,
        p.morph,
        temp_buffer_,
        destination,
        size);
    newly_triggered_[i] = false;
    if (stereo) {
      // Pan the string, and clear the buffer for the next one.
      const float l = string_pan[i][0];
      const float r = string_pan[i][1];
      if (i == 0) {
        for (size_t j = 0; j < size; ++j) {
          const float s = string_buffer_[j];
          string_buffer_[j] = 0.0f;
          out[j] = l * s;
          aux[j] = r * s;
        }
      } else {
        for (size_t j = 0; j < size; ++j) {
          const float s = string_buffer_[j];
          string_buffer_[j] = 0.0f;
          out[j] += l * s;
          aux[j] += r * s;
        }
      }
    }
  }
  
  if (stereo) {
    quadrature_.Process(out, aux, size);
  }
}

}  // namespace plaits
