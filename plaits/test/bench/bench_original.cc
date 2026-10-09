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
// CPU use of the 16 models of the original firmware, for a grid of HARMONICS,
// TIMBRE, MORPH and pitch values, with and without triggers. Builds with the
// source of this firmware, and with the source of the original firmware (the
// upstream-v1.1 tag). Each measured block is tagged with model << 8 |
// setting.

#include "plaits/dsp/voice.h"

using namespace plaits;
using namespace stmlib;

extern "C" void bench_mark(int tag);

Voice voice;
char shared_buffer[16384];
Patch patch;
Modulations modulations;
Voice::Frame frames[kMaxBlockSize];

const float kHarmonics[] = {
  0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f
};
const float kTimbreMorph[][2] = {
  { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f },
  { 0.25f, 0.75f }, { 0.5f, 0.5f }, { 0.75f, 0.25f }
};

int main() {
  BufferAllocator allocator(shared_buffer, 16384);
  voice.Init(&allocator);

  patch.decay = 0.5f;
  patch.lpg_colour = 0.5f;
  modulations.engine = 0.0f;
  modulations.note = 0.0f;
  modulations.frequency = 0.0f;
  modulations.harmonics = 0.0f;
  modulations.timbre = 0.0f;
  modulations.morph = 0.0f;
  modulations.trigger = 0.0f;
  modulations.level = 0.0f;
  modulations.frequency_patched = false;
  modulations.timbre_patched = false;
  modulations.morph_patched = false;
  modulations.level_patched = false;

  for (int model = 0; model < 16; ++model) {
    patch.engine = model;
    int setting = 0;
    for (int trigger = 0; trigger < 2; ++trigger) {
      for (int h = 0; h < 11; ++h) {
        for (int tm = 0; tm < 7; ++tm) {
          patch.harmonics = kHarmonics[h];
          patch.timbre = kTimbreMorph[tm][0];
          patch.morph = kTimbreMorph[tm][1];
          patch.note = 24.0f + 12.0f * float((tm + h) % 6);
          modulations.trigger_patched = trigger;
          modulations.trigger = 0.0f;
          const int tag = (model << 8) | setting;
          for (int i = 0; i < 8; ++i) {
            voice.Render(patch, modulations, frames, kBlockSize);
          }
          bench_mark(0x10000 | tag);
          for (int i = 0; i < 24; ++i) {
            modulations.trigger = (trigger && i < 4) ? 1.0f : 0.0f;
            voice.Render(patch, modulations, frames, kBlockSize);
            bench_mark(tag);
          }
          ++setting;
        }
      }
    }
  }
  return 0;
}
