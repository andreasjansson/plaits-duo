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
// Duo version of the wavetable oscillator.
//
// Differences with WavetableEngine::Render:
// - The position in the wavetable (TIMBRE, MORPH, HARMONICS), its smoothing,
//   and the weights of the 8 surrounding waves are computed once per sample
//   for the two voices. The smoothing rate depends on pitch in the original
//   engine: the pitch of the first voice is used.
// - The addresses of the 8 waves are only recomputed when the position moves
//   to another cell of the wavetable (the original code does 8 integer
//   modulos per sample).
// - Hermite interpolation being linear in the samples, the 4 samples used by
//   the interpolator are blended across the 8 waves before interpolating,
//   instead of interpolating each wave and blending the 8 results. The
//   blending is done in fixed point (the waves are stored as int16), which
//   saves 32 int to float conversions per sample and uses the single-cycle
//   multiply-accumulate of the Cortex-M4 (SMLAWB). It has 14 more bits of
//   precision than the int16 samples.
// - The gain compensating for the differentiator, which is inversely
//   proportional to the frequency, is computed with one Newton-Raphson
//   iteration per sample from the previous value instead of a division.

#include "plaits/dsp/duo/duo_wavetable_engine.h"

#include <algorithm>
#include <cstring>

#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

const size_t kDuoTableSize = 256;
const float kDuoTableSizeF = float(kDuoTableSize);
const size_t kDuoWaveSize = kDuoTableSize + 4;

// Same as Clamp() in wavetable_engine.cc.
inline float DuoWTClamp(float x, float amount) {
  x = x - 0.5f;
  x *= amount;
  CONSTRAIN(x, -0.5f, 0.5f);
  x += 0.5f;
  return x;
}

inline int DuoWTWaveNumber(int x, int y, int z) {
  if (z >= 4) {
    z = 7 - z;
  }
  const int randomize = z == 3 ? 101 : 1;
  return ((x + y * 8 + z * 64) * randomize) % 192;
}

// acc + (a * b[15:0]) >> 16, with a 48-bit intermediate product.
inline int32_t Smlawb(int32_t a, uint32_t b, int32_t acc) {
#ifdef TEST
  return acc + static_cast<int32_t>(
      (static_cast<int64_t>(a) * static_cast<int16_t>(b)) >> 16);
#else
  int32_t result;
  __asm__("smlawb %0, %1, %2, %3" : "=r" (result) : "r" (a), "r" (b), "r" (acc));
  return result;
#endif  // TEST
}

// The same with b[31:16].
inline int32_t Smlawt(int32_t a, uint32_t b, int32_t acc) {
#ifdef TEST
  return acc + static_cast<int32_t>(
      (static_cast<int64_t>(a) * static_cast<int16_t>(b >> 16)) >> 16);
#else
  int32_t result;
  __asm__("smlawt %0, %1, %2, %3" : "=r" (result) : "r" (a), "r" (b), "r" (acc));
  return result;
#endif  // TEST
}

// Two consecutive samples (the first in the low half), at any address: the
// processor reads unaligned words.
inline uint32_t ReadPair(const int16_t* p) {
  uint32_t pair;
  memcpy(&pair, p, sizeof(pair));
  return pair;
}

}  // namespace

void DuoWavetableVoice::Init() {
  phase_ = 0.0f;
  previous_f0_ = a0;
  reciprocal_f0_ = 1.0f / a0;
  diff_lp_ = 0.0f;
  diff_previous_ = 0.0f;
}

void DuoWavetableVoice::Render(
    float f0,
    const DuoWavetableBlend* blend,
    bool restart,
    float* out,
    size_t size) {
  ParameterInterpolator f0_modulation(&previous_f0_, f0, size);
  
  // Large jumps would need several iterations to converge: use a division
  // for the duration of the block.
  float reciprocal_f0 = reciprocal_f0_;
  const bool large_jump = f0 * reciprocal_f0 > 1.5f || \
      f0 * reciprocal_f0 < 0.66f;
  
  // The state in local variables (out could point to the members).
  float phase = phase_;
  float diff_lp = diff_lp_;
  float diff_previous = diff_previous_;
  while (size--) {
    const float f0 = f0_modulation.Next();
    reciprocal_f0 = large_jump
        ? 1.0f / f0
        : reciprocal_f0 * (2.0f - f0 * reciprocal_f0);
    // Includes the 2^-14 scale of the fixed point blend.
    const float gain = reciprocal_f0 * (0.95f - f0) * 4.656612873e-10f;
    const float cutoff = min(kDuoTableSizeF * f0, 1.0f);
    
    phase += f0;
    if (phase >= 1.0f) {
      phase -= 1.0f;
    }
    
    const float p = phase * kDuoTableSizeF;
    MAKE_INTEGRAL_FRACTIONAL(p);
    
    int32_t s0 = 0;
    int32_t s1 = 0;
    int32_t s2 = 0;
    int32_t s3 = 0;
    const int16_t* const* wave = blend->wave;
    for (int j = 0; j < 8; ++j) {
      const int16_t* w = wave[j] + p_integral;
      const uint32_t w01 = ReadPair(w);
      const uint32_t w23 = ReadPair(w + 2);
      const int32_t weight = blend->weight[j];
      s0 = Smlawb(weight, w01, s0);
      s1 = Smlawt(weight, w01, s1);
      s2 = Smlawb(weight, w23, s2);
      s3 = Smlawt(weight, w23, s3);
    }
    ++blend;
    
    // Same as InterpolateWaveHermite.
    const float xm1 = static_cast<float>(s0);
    const float x0 = static_cast<float>(s1);
    const float x1 = static_cast<float>(s2);
    const float x2 = static_cast<float>(s3);
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b_neg = w + a;
    const float mix = (((a * p_fractional) - b_neg) * p_fractional + c) * \
        p_fractional + x0;
    
    if (restart) {
      // The differentiator starts from this sample, rather than from the
      // last one rendered long ago: no click.
      diff_lp = 0.0f;
      diff_previous = 0.0f;
      ONE_POLE(diff_lp, mix - diff_previous, 0.0f);
      diff_previous = mix;
      restart = false;
    }
    // Same as Differentiator::Process.
    ONE_POLE(diff_lp, mix - diff_previous, cutoff);
    diff_previous = mix;
    *out++ = diff_lp * gain;
  }
  phase_ = phase;
  reciprocal_f0_ = reciprocal_f0;
  diff_lp_ = diff_lp;
  diff_previous_ = diff_previous;
}

void DuoWavetableEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kNumDuoVoices; ++i) {
    voice_[i].Init();
  }
  cache_ = allocator->Allocate<int16_t>(8 * kDuoWaveSize);
  if ((reinterpret_cast<uintptr_t>(cache_) | \
       reinterpret_cast<uintptr_t>(wav_integrated_waves)) & 3) {
    cache_ = NULL;  // Copied as 32-bit words.
  }
  fill(&cached_wave_[0], &cached_wave_[8], -1);
  
  x_lp_ = 0.0f;
  y_lp_ = 0.0f;
  z_lp_ = 0.0f;
  
  x_pre_lp_ = 0.0f;
  y_pre_lp_ = 0.0f;
  z_pre_lp_ = 0.0f;

  previous_x_ = 0.0f;
  previous_y_ = 0.0f;
  previous_z_ = 0.0f;
  
  UpdateWaves(0, 0, 0);
  second_voice_active_ = false;
}

void DuoWavetableEngine::Reset() {
  second_voice_active_ = false;
  // Another engine used the RAM.
  fill(&cached_wave_[0], &cached_wave_[8], -1);
  UpdateWaves(cell_ & 15, (cell_ >> 4) & 15, cell_ >> 8);
}

void DuoWavetableEngine::UpdateWaves(int x, int y, int z) {
  cell_ = x | (y << 4) | (z << 8);
  // In the order of the blend: x, then y, then z.
  for (int j = 0; j < 8; ++j) {
    wave_number_[j] = DuoWTWaveNumber(x + (j & 1), y + ((j >> 1) & 1), z + (j >> 2));
  }
  if (!cache_) {
    for (int j = 0; j < 8; ++j) {
      wave_[j] = wav_integrated_waves + wave_number_[j] * kDuoWaveSize;
    }
    return;
  }
  // The slot of each wave of the cell: the one which has it, or one which
  // has none of the waves of the cell (bit s of used: slot s has one).
  int slot[8];
  uint32_t used = 0;
  for (int j = 0; j < 8; ++j) {
    slot[j] = -1;
    for (int s = 0; s < 8; ++s) {
      if (cached_wave_[s] == wave_number_[j]) {
        slot[j] = s;
        used |= 1 << s;
        break;
      }
    }
  }
  for (int j = 0; j < 8; ++j) {
    if (slot[j] >= 0) {
      continue;
    }
    // (Unless an earlier wave of the cell is the same.)
    for (int k = 0; k < j; ++k) {
      if (wave_number_[k] == wave_number_[j]) {
        slot[j] = slot[k];
        break;
      }
    }
    if (slot[j] < 0) {
      const int s = __builtin_ctz(~used);
      cached_wave_[s] = wave_number_[j];
      copied_[s] = 0;
      used |= 1 << s;
      slot[j] = s;
    }
  }
  for (int j = 0; j < 8; ++j) {
    const int s = slot[j];
    wave_[j] = copied_[s] == static_cast<int16_t>(kDuoWaveSize)
        ? cache_ + s * kDuoWaveSize
        : wav_integrated_waves + wave_number_[j] * kDuoWaveSize;
  }
}

// 8 words, which a struct copy moves with one LDM and one STM: much faster
// than single words from the flash memory, whose reads stall the processor.
struct DuoWords {
  uint32_t word[8];
};

void DuoWavetableEngine::FillCache() {
  if (!cache_) {
    return;
  }
  // A wave per block (about 500 cycles), as 32-bit words (the waves start at
  // a multiple of 4 bytes).
  size_t budget = kDuoWaveSize;
  for (int s = 0; s < 8 && budget; ++s) {
    if (cached_wave_[s] < 0 || \
        copied_[s] == static_cast<int16_t>(kDuoWaveSize)) {
      continue;
    }
    size_t n = min(budget, kDuoWaveSize - copied_[s]);
    const uint32_t* from = reinterpret_cast<const uint32_t*>(
        wav_integrated_waves + cached_wave_[s] * kDuoWaveSize + copied_[s]);
    uint32_t* to = reinterpret_cast<uint32_t*>(
        cache_ + s * kDuoWaveSize + copied_[s]);
    copied_[s] += n;
    budget -= n;
    size_t words = n / 2;
    for (; words >= 8; words -= 8) {
      *reinterpret_cast<DuoWords*>(to) = \
          *reinterpret_cast<const DuoWords*>(from);
      to += 8;
      from += 8;
    }
    while (words--) {
      *to++ = *from++;
    }
    if (copied_[s] == static_cast<int16_t>(kDuoWaveSize)) {
      for (int j = 0; j < 8; ++j) {
        if (wave_number_[j] == cached_wave_[s]) {
          wave_[j] = cache_ + s * kDuoWaveSize;
        }
      }
    }
  }
}

void DuoWavetableEngine::ComputeBlend(
    const EngineParameters& parameters,
    DuoWavetableBlend* blend,
    const int16_t* (*sets)[8],
    size_t size) {
  const float f0 = NoteToFrequency(parameters.note);
  
  ONE_POLE(x_pre_lp_, parameters.timbre * 6.9999f, 0.2f);
  ONE_POLE(y_pre_lp_, parameters.morph * 6.9999f, 0.2f);
  ONE_POLE(z_pre_lp_, parameters.harmonics * 6.9999f, 0.05f);
  
  const float x = x_pre_lp_;
  const float y = y_pre_lp_;
  const float z = z_pre_lp_;
  
  const float quantization = min(max(z - 3.0f, 0.0f), 1.0f);
  const float lp_coefficient = min(
      max(2.0f * f0 * (4.0f - 3.0f * quantization), 0.01f), 0.1f);
  
  MAKE_INTEGRAL_FRACTIONAL(x);
  MAKE_INTEGRAL_FRACTIONAL(y);
  MAKE_INTEGRAL_FRACTIONAL(z);
  
  x_fractional += quantization * (DuoWTClamp(x_fractional, 16.0f) - x_fractional);
  y_fractional += quantization * (DuoWTClamp(y_fractional, 16.0f) - y_fractional);
  z_fractional += quantization * (DuoWTClamp(z_fractional, 16.0f) - z_fractional);
  
  ParameterInterpolator x_modulation(
      &previous_x_, static_cast<float>(x_integral) + x_fractional, size);
  ParameterInterpolator y_modulation(
      &previous_y_, static_cast<float>(y_integral) + y_fractional, size);
  ParameterInterpolator z_modulation(
      &previous_z_, static_cast<float>(z_integral) + z_fractional, size);
  
  float x_lp = x_lp_;
  float y_lp = y_lp_;
  float z_lp = z_lp_;
  
  // (Loops: std::copy would call memmove, which newlib-nano does a byte at a
  // time.)
  for (int j = 0; j < 8; ++j) {
    (*sets)[j] = wave_[j];
  }
  const int16_t* const* set = *sets;
  for (size_t i = 0; i < size; ++i) {
    ONE_POLE(x_lp, x_modulation.Next(), lp_coefficient);
    ONE_POLE(y_lp, y_modulation.Next(), lp_coefficient);
    ONE_POLE(z_lp, z_modulation.Next(), lp_coefficient);
    
    const float x = x_lp;
    const float y = y_lp;
    const float z = z_lp;

    MAKE_INTEGRAL_FRACTIONAL(x);
    MAKE_INTEGRAL_FRACTIONAL(y);
    MAKE_INTEGRAL_FRACTIONAL(z);
    
    const int cell = x_integral | (y_integral << 4) | (z_integral << 8);
    if (cell != cell_) {
      UpdateWaves(x_integral, y_integral, z_integral);
      ++sets;
      for (int j = 0; j < 8; ++j) {
        (*sets)[j] = wave_[j];
      }
      set = *sets;
    }
    
    const float scale = 1073741824.0f;
    const float x0 = (1.0f - x_fractional) * scale;
    const float x1 = x_fractional * scale;
    const float y0z0 = (1.0f - y_fractional) * (1.0f - z_fractional);
    const float y1z0 = y_fractional * (1.0f - z_fractional);
    const float y0z1 = (1.0f - y_fractional) * z_fractional;
    const float y1z1 = y_fractional * z_fractional;
    
    DuoWavetableBlend* b = &blend[i];
    b->wave = set;
    b->weight[0] = static_cast<int32_t>(x0 * y0z0);
    b->weight[1] = static_cast<int32_t>(x1 * y0z0);
    b->weight[2] = static_cast<int32_t>(x0 * y1z0);
    b->weight[3] = static_cast<int32_t>(x1 * y1z0);
    b->weight[4] = static_cast<int32_t>(x0 * y0z1);
    b->weight[5] = static_cast<int32_t>(x1 * y0z1);
    b->weight[6] = static_cast<int32_t>(x0 * y1z1);
    b->weight[7] = static_cast<int32_t>(x1 * y1z1);
  }
  
  x_lp_ = x_lp;
  y_lp_ = y_lp;
  z_lp_ = z_lp;
}

void DuoWavetableEngine::Render(
    const EngineParameters* parameters,
    int num_voices,
    bool stereo,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  DuoWavetableBlend blend[kMaxBlockSize];
  const int16_t* sets[kMaxBlockSize + 1][8];
  FillCache();
  ComputeBlend(parameters[0], blend, sets, size);
  
  // The first voice starts as in the original engine (with a click, in the
  // split layout), the second one without a click when LEVEL is patched.
  const bool restart_second_voice = num_voices == 2 && !second_voice_active_;
  second_voice_active_ = num_voices == 2;
  for (int i = 0; i < num_voices; ++i) {
    voice_[i].Render(
        NoteToFrequency(parameters[i].note),
        blend,
        i == 1 && restart_second_voice,
        i == 0 ? out : aux,
        size);
  }
}

}  // namespace plaits
