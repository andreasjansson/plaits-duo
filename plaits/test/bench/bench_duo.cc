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
// CPU use of the duo mode models: each model, with one and two voices, in the
// stereo and split layouts, for a grid of HARMONICS, TIMBRE and MORPH values,
// with and without triggers, with pitch jumps. Each measured block is tagged
// with (voices - 1) << 15 | layout << 11 | model << 8 | setting.

#include "plaits/dsp/duo_voice.h"

using namespace plaits;
using namespace stmlib;

extern "C" void bench_mark(int tag);

Voice voice;
DuoVoice duo_voice;
char shared_buffer[16384];
Patch patch;
Modulations modulations;
Voice::Frame frames[kMaxBlockSize];

const float kHarmonics[] = { 0.05f, 0.25f, 0.4f, 0.5f, 0.75f, 0.95f };
const float kTimbreMorph[][2] = {
  { 0.1f, 0.1f }, { 0.5f, 0.5f }, { 0.9f, 0.9f }, { 0.1f, 0.9f }
};

int main() {
  BufferAllocator allocator(shared_buffer, 16384);
  voice.Init(&allocator);
  duo_voice.Init(&allocator);

  patch.note = 48.0f;
  patch.decay = 0.5f;
  patch.lpg_colour = 0.5f;
  patch.duo_mode = true;
  for (int voices = 1; voices <= 2; ++voices) {
    modulations.level_patched = voices == 2;
    for (int model = 0; model < 8; ++model) {
      for (int layout = 0; layout < 2; ++layout) {
        // Bit 3 of duo_engine: split layout.
        patch.duo_engine = model | (layout ? 8 : 0);
        int setting = 0;
        for (int trigger = 0; trigger < 2; ++trigger) {
          for (int h = 0; h < 6; ++h) {
            for (int tm = 0; tm < 4; ++tm) {
              patch.harmonics = kHarmonics[h];
              patch.timbre = kTimbreMorph[tm][0];
              patch.morph = kTimbreMorph[tm][1];
              modulations.trigger_patched = trigger;
              modulations.trigger = 0.0f;
              const int tag = ((voices - 1) << 15) | \
                  (patch.duo_engine << 8) | setting;
              for (int i = 0; i < 8; ++i) {
                modulations.note = 12.0f * float(tm);
                modulations.level_note = 7.0f + 5.0f * float(h);
                duo_voice.Render(patch, modulations, frames, kBlockSize);
              }
              bench_mark(0x10000 | tag);
              for (int i = 0; i < 24; ++i) {
                modulations.trigger = (trigger && i < 4) ? 1.0f : 0.0f;
                if (i == 12) {
                  // Pitch jumps, including large ones.
                  modulations.note = -12.0f + 30.0f * float(tm & 1);
                  modulations.level_note = 36.0f - 9.0f * float(h);
                }
                duo_voice.Render(patch, modulations, frames, kBlockSize);
                bench_mark(tag);
              }
              ++setting;
            }
          }
        }
      }
    }
  }
  return 0;
}
