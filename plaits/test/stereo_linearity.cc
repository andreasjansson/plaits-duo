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
// plaits-stereo-linearity: checks that nothing clips inside the stereo
// effects of duo mode (see plaits/dsp/duo/duo_stereo.h). Each effect
// processes 10 seconds of the two voices at full scale (two square waves a
// fifth apart: their sum reaches twice full scale), then the same signal at a
// quarter of the level. The effects are linear, and scaling by a power of 2
// is exact in floating point: unless something clips, the second output is
// exactly a quarter of the first one.
//
// Usage: plaits-stereo-linearity
//
// Prints, for each effect, the largest difference between the first output
// and four times the second one, in units of the least significant bit of a
// 16-bit sample (1/32768).

#include <cmath>
#include <cstdio>
#include <vector>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/duo/duo_stereo.h"

using namespace plaits;
using namespace std;

const size_t kNumBlocks = static_cast<size_t>(10.0f * kSampleRate) /
    kBlockSize;

// The left and right channels of the effect, for the voices at the given
// level.
void Render(DuoStereoEffect effect, float level, vector<float>* out) {
  static float diffuser_memory[kDuoDiffuserMemorySize];
  static DuoStereo stereo;
  stereo.Init(diffuser_memory);
  stereo.set_effect(effect);
  out->clear();
  float phase[2] = { 0.0f, 0.0f };
  const float frequency[2] = { 220.0f / kSampleRate, 330.0f / kSampleRate };
  for (size_t block = 0; block < kNumBlocks; ++block) {
    float left[kBlockSize];
    float right[kBlockSize];
    for (size_t i = 0; i < kBlockSize; ++i) {
      left[i] = 0.0f;
      for (int v = 0; v < 2; ++v) {
        phase[v] += frequency[v];
        if (phase[v] >= 1.0f) {
          phase[v] -= 1.0f;
        }
        left[i] += phase[v] < 0.5f ? level : -level;
      }
    }
    stereo.Process(left, right, kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
      out->push_back(left[i]);
      out->push_back(right[i]);
    }
  }
}

int main() {
  const struct {
    DuoStereoEffect effect;
    const char* name;
  } effects[] = {
    { DUO_STEREO_EFFECT_DIFFUSER, "diffuser" },
    { DUO_STEREO_EFFECT_CHORUS, "chorus" },
    { DUO_STEREO_EFFECT_ENSEMBLE, "ensemble" },
  };
  for (size_t e = 0; e < sizeof(effects) / sizeof(effects[0]); ++e) {
    vector<float> loud, quiet;
    Render(effects[e].effect, 1.0f, &loud);
    Render(effects[e].effect, 0.25f, &quiet);
    double difference = 0.0;
    for (size_t i = 0; i < loud.size(); ++i) {
      difference = max(difference, fabs(loud[i] - 4.0 * quiet[i]));
    }
    printf("%s: largest difference %.0f LSB\n",
           effects[e].name, ceil(difference * 32768.0));
  }
  return 0;
}
