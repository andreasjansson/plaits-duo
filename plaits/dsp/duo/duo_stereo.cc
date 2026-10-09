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
// -----------------------------------------------------------------------------
//
// Stereo effects of duo mode.

#include "plaits/dsp/duo/duo_stereo.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"

#include "plaits/dsp/dsp.h"
#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

// Diffuser: the two allpass sections of each chain (left: 69 + 176 samples,
// right: 140 + 105 samples, the same total), their gain, and how far and
// how fast the delays move. Moving delays slightly change the pitch: by at
// most 3.4 cents with these settings.
const float kDiffuserDelay[4] = { 69.0f, 176.0f, 140.0f, 105.0f };
const float kDiffuserGain = 0.5f;
const float kDiffuserDepth = 48.0f;
const float kDiffuserRate[2] = { 0.23f, 0.31f };

// Chorus: delays in samples, LFO rate.
const float kChorusCenter = 3.505e-3f * kCorrectedSampleRate;
const float kChorusDepth = 1.845e-3f * kCorrectedSampleRate;
const float kChorusRate = 0.513f;

// Ensemble: center delay, modulation of the slow and fast LFOs (for a depth
// of 0.6 in plaits/dsp/fx/ensemble.h) and their rates, dry and wet levels
// (for an amount of 0.7).
const float kEnsembleCenter = 192.0f;
const float kEnsembleSlowDepth = 96.0f;
const float kEnsembleFastDepth = 9.6f;
const float kEnsembleRate[2] = { 0.75f, 6.57f };
const float kEnsembleDry = 0.65f;
const float kEnsembleWet = 0.7f / 3.0f;

// One-pole coefficient for a 150Hz cutoff, to keep the bass out of the
// effects.
const float kBassCutoff = 150.0f / kCorrectedSampleRate * 6.2831853f;

// phase in cycles, between 0 and 2.
inline float Sine(float phase) {
  if (phase >= 1.0f) {
    phase -= 1.0f;
  }
  return Interpolate(lut_sine, phase, 1024.0f);
}

inline float Triangle(float phase) {
  return phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
}

// Length of a delay line, for a maximum delay (one more sample for the
// interpolation).
inline size_t DelayLength(float max_delay) {
  return static_cast<size_t>(max_delay) + 2;
}

void DuoAllpassLine::Init(float* memory, size_t length) {
  begin = memory;
  end = memory + length;
  write = memory;
  fill(begin, end, 0.0f);
}

void DuoDelay::Init(float* memory, size_t length) {
  fill(&memory[0], &memory[memory_size(length)], 0.0f);
  begin_ = memory + 1;
  end_ = begin_ + length;
  write_ = begin_;
}

void DuoDelay::Write(const float* in, size_t size) {
  float* write = write_;
  // The first samples are copied after the end of the line, and the last
  // sample before its start.
  const float* guard = begin_ + kDuoDelayGuard;
  const size_t length = end_ - begin_;
  while (size) {
    // Up to the end of the line.
    size_t n = min(size, static_cast<size_t>(end_ - write));
    size -= n;
    while (n--) {
      const float x = *in++;
      if (write < guard) {
        write[length] = x;
      }
      *write++ = x;
    }
    if (write == end_) {
      begin_[-1] = end_[-1];
      write = begin_;
    }
  }
  write_ = write;
}

/* static */
__attribute__((noinline)) float DuoDelay::Approach(float delay, float target) {
  const float integral = static_cast<float>(static_cast<int>(delay));
  if (target > delay) {
    return min(target, integral + 1.0f);
  } else {
    return max(target, integral == delay ? integral - 1.0f : integral);
  }
}

template<int num_taps>
void DuoDelay::Read(
    const float* delay,
    const float* target,
    float* out,
    size_t size) const {
  // A tap reads between the samples delayed by integral and integral + 1,
  // and moves linearly between them through the block.
  struct Tap {
    const float* read;
    float previous;
    float fractional;
    float increment;
  } taps[num_taps];
  const float scale = 1.0f / static_cast<float>(size);
  for (int k = 0; k < num_taps; ++k) {
    Tap& t = taps[k];
    int integral = static_cast<int>(delay[k]);
    if (target[k] < delay[k] && static_cast<float>(integral) == delay[k]) {
      // Going down from a whole number of samples.
      --integral;
    }
    t.fractional = delay[k] - static_cast<float>(integral);
    t.increment = (target[k] - delay[k]) * scale;
    // The copies are there for reading across the ends of the line.
    t.read = write_ - integral;
    if (t.read < begin_) {
      t.read += end_ - begin_;
    }
    t.previous = t.read[-1];
  }
  for (size_t i = 0; i < size; ++i) {
    float sum = 0.0f;
    for (int k = 0; k < num_taps; ++k) {
      Tap& t = taps[k];
      const float current = *t.read++;
      sum += current + t.fractional * (t.previous - current);
      t.previous = current;
      t.fractional += t.increment;
    }
    out[i] = sum;
  }
}

template void DuoDelay::Read<1>(
    const float*, const float*, float*, size_t) const;
template void DuoDelay::Read<3>(
    const float*, const float*, float*, size_t) const;

void DuoStereo::Init(float* diffuser_memory) {
  diffuser_memory_ = diffuser_memory;
  peak_ = 0.5f;
  set_effect(DUO_STEREO_EFFECT_NONE);
}

void DuoStereo::set_effect(DuoStereoEffect effect) {
  effect_ = effect;
  peak_ = 0.5f;
  phase_[0] = phase_[1] = 0.0f;
  filter_state_[0] = filter_state_[1] = 0.0f;
  for (int i = 0; i < 2; ++i) {
    lfo_sine_[i] = 0.0f;
    lfo_cosine_[i] = 1.0f;
  }
  // The delays of the taps, for LFOs at a phase of 0 (see ProcessChorus and
  // ProcessEnsemble).
  const float b = 0.866025f * (kEnsembleSlowDepth + kEnsembleFastDepth);
  const float m[3] = { 0.0f, b, -b };
  for (int k = 0; k < 3; ++k) {
    if (effect_ == DUO_STEREO_EFFECT_CHORUS) {
      tap_delay_[0][k] = kChorusCenter - kChorusDepth;
      tap_delay_[1][k] = kChorusCenter + kChorusDepth;
    } else {
      tap_delay_[0][k] = kEnsembleCenter + m[k];
      tap_delay_[1][k] = kEnsembleCenter - m[k];
    }
  }

  // The delay lines (if their memory is too small, which does not happen with
  // these settings, there is no effect).
  if (effect_ == DUO_STEREO_EFFECT_DIFFUSER) {
    // Left chain: sections 0 and 1, right chain: sections 2 and 3.
    size_t length[4];
    for (int i = 0; i < 4; ++i) {
      length[i] = DelayLength(kDiffuserDelay[i] + kDiffuserDepth);
    }
    if (!diffuser_memory_ ||
        length[0] + length[2] > kDuoStereoMemorySize ||
        length[1] + length[3] > kDuoDiffuserMemorySize) {
      effect_ = DUO_STEREO_EFFECT_NONE;
      return;
    }
    allpass_[0].Init(memory_, length[0]);
    allpass_[2].Init(memory_ + length[0], length[2]);
    allpass_[1].Init(diffuser_memory_, length[1]);
    allpass_[3].Init(diffuser_memory_ + length[1], length[3]);
  } else if (effect_ != DUO_STEREO_EFFECT_NONE) {
    const size_t length = effect_ == DUO_STEREO_EFFECT_CHORUS
        ? DelayLength(kChorusCenter + kChorusDepth)
        : DelayLength(
            kEnsembleCenter + kEnsembleSlowDepth + kEnsembleFastDepth);
    if (DuoDelay::memory_size(length) > kDuoStereoMemorySize) {
      effect_ = DUO_STEREO_EFFECT_NONE;
      return;
    }
    delay_.Init(memory_, length);
  }
}

void DuoStereo::Process(float* left, float* right, size_t size) {
  switch (effect_) {
    case DUO_STEREO_EFFECT_DIFFUSER:
      ProcessDiffuser(left, right, size);
      break;
    case DUO_STEREO_EFFECT_CHORUS:
      ProcessChorus(left, right, size);
      break;
    case DUO_STEREO_EFFECT_ENSEMBLE:
      ProcessEnsemble(left, right, size);
      break;
    default:
      copy(&left[0], &left[size], &right[0]);
      break;
  }
}

// Schroeder allpass sections: w[n] = x[n] + g * w[n - delay],
// y[n] = w[n - delay] - g * w[n].
void DuoStereo::ProcessAllpassChain(
    DuoAllpassLine* a,
    float delay_a,
    DuoAllpassLine* b,
    float delay_b,
    const float* in,
    float* out,
    size_t size) {
  const float g = kDiffuserGain;
  const int integral_a = static_cast<int>(delay_a);
  const int integral_b = static_cast<int>(delay_b);
  const float fractional_a = delay_a - static_cast<float>(integral_a);
  const float fractional_b = delay_b - static_cast<float>(integral_b);
  const float complement_a = 1.0f - fractional_a;
  const float complement_b = 1.0f - fractional_b;
  float* read_a = a->tap(integral_a);
  float* read_b = b->tap(integral_b);
  float* write_a = a->write;
  float* write_b = b->write;
  float previous_a = *a->tap(integral_a + 1);
  float previous_b = *b->tap(integral_b + 1);
  while (size) {
    // Up to the end of a line.
    size_t n = min(size, static_cast<size_t>(a->end - read_a));
    n = min(n, static_cast<size_t>(a->end - write_a));
    n = min(n, static_cast<size_t>(b->end - read_b));
    n = min(n, static_cast<size_t>(b->end - write_b));
    size -= n;
    while (n--) {
      const float x = *in++;

      const float delayed_a = *read_a++;
      const float d_a = previous_a * fractional_a + delayed_a * complement_a;
      previous_a = delayed_a;
      const float w_a = x + g * d_a;
      *write_a++ = w_a;
      const float y_a = d_a - g * w_a;

      const float delayed_b = *read_b++;
      const float d_b = previous_b * fractional_b + delayed_b * complement_b;
      previous_b = delayed_b;
      const float w_b = y_a + g * d_b;
      *write_b++ = w_b;
      *out++ = d_b - g * w_b;
    }
    if (read_a == a->end) {
      read_a = a->begin;
    }
    if (write_a == a->end) {
      write_a = a->begin;
    }
    if (read_b == b->end) {
      read_b = b->begin;
    }
    if (write_b == b->end) {
      write_b = b->begin;
    }
  }
  a->write = write_a;
  b->write = write_b;
}

void DuoStereo::ProcessDiffuser(float* left, float* right, size_t size) {
  const float block = static_cast<float>(size) / kCorrectedSampleRate;
  float m[2];
  for (int i = 0; i < 2; ++i) {
    phase_[i] += kDiffuserRate[i] * block;
    if (phase_[i] >= 1.0f) {
      phase_[i] -= 1.0f;
    }
    m[i] = kDiffuserDepth * Sine(phase_[i]);
  }
  // The right channel first, since the left channel is processed in place.
  ProcessAllpassChain(
      &allpass_[2], kDiffuserDelay[2] + m[1],
      &allpass_[3], kDiffuserDelay[3] - m[1],
      left, right, size);
  ProcessAllpassChain(
      &allpass_[0], kDiffuserDelay[0] + m[0],
      &allpass_[1], kDiffuserDelay[1] - m[0],
      left, left, size);
}

void DuoStereo::ProcessChorus(float* left, float* right, size_t size) {
  phase_[0] += kChorusRate / kCorrectedSampleRate * static_cast<float>(size);
  if (phase_[0] >= 1.0f) {
    phase_[0] -= 1.0f;
  }
  const float m = kChorusDepth * Triangle(phase_[0]);
  float target[2];
  target[0] = DuoDelay::Approach(tap_delay_[0][0], kChorusCenter + m);
  target[1] = DuoDelay::Approach(tap_delay_[1][0], kChorusCenter - m);

  float wet_l[kMaxBlockSize];
  float wet_r[kMaxBlockSize];
  delay_.Read<1>(&tap_delay_[0][0], &target[0], wet_l, size);
  delay_.Read<1>(&tap_delay_[1][0], &target[1], wet_r, size);
  tap_delay_[0][0] = target[0];
  tap_delay_[1][0] = target[1];

  // The BBDs of the original are followed by low-pass filters.
  float wet_lp = filter_state_[0];
  float side_lp = filter_state_[1];
  float wet[kMaxBlockSize];
  for (size_t i = 0; i < size; ++i) {
    const float x = left[i];
    wet_lp += 0.6f * (x - wet_lp);
    wet[i] = wet_lp;

    const float mid = 0.7f * x + 0.35f * (wet_l[i] + wet_r[i]);
    const float side = 0.35f * (wet_l[i] - wet_r[i]);
    side_lp += kBassCutoff * (side - side_lp);
    left[i] = mid + (side - side_lp);
    right[i] = mid - (side - side_lp);
  }
  filter_state_[0] = wet_lp;
  filter_state_[1] = side_lp;
  delay_.Write(wet, size);
}

void DuoStereo::ProcessEnsemble(float* left, float* right, size_t size) {
  // The two LFOs are vectors (their sine and cosine), turned by a small angle
  // at each block (whose sine is close to the angle, and cosine to 1 -
  // angle^2 / 2), and brought back to the unit circle.
  const float block = static_cast<float>(size) * \
      (6.2831853f / kCorrectedSampleRate);
  for (int i = 0; i < 2; ++i) {
    const float angle = kEnsembleRate[i] * block;
    const float cosine = 1.0f - 0.5f * angle * angle;
    const float x = lfo_sine_[i] * cosine + lfo_cosine_[i] * angle;
    const float y = lfo_cosine_[i] * cosine - lfo_sine_[i] * angle;
    const float gain = 1.5f - 0.5f * (x * x + y * y);
    lfo_sine_[i] = x * gain;
    lfo_cosine_[i] = y * gain;
  }
  // The modulation of the three taps at the end of the block (the taps are
  // 120 degrees apart).
  const float a = \
      kEnsembleSlowDepth * lfo_sine_[0] + kEnsembleFastDepth * lfo_sine_[1];
  const float b = 0.866025f * (
      kEnsembleSlowDepth * lfo_cosine_[0] + \
      kEnsembleFastDepth * lfo_cosine_[1]);
  const float m[3] = { a, -0.5f * a + b, -0.5f * a - b };
  float target[2][3];
  for (int k = 0; k < 3; ++k) {
    target[0][k] = DuoDelay::Approach(
        tap_delay_[0][k], kEnsembleCenter + m[k]);
    target[1][k] = DuoDelay::Approach(
        tap_delay_[1][k], kEnsembleCenter - m[k]);
  }

  float wet_l[kMaxBlockSize];
  float wet_r[kMaxBlockSize];
  delay_.Read<3>(tap_delay_[0], target[0], wet_l, size);
  delay_.Read<3>(tap_delay_[1], target[1], wet_r, size);
  copy(&target[0][0], &target[2][0], &tap_delay_[0][0]);

  // The bass is kept out of the delay line.
  float lp = filter_state_[0];
  float in[kMaxBlockSize];
  for (size_t i = 0; i < size; ++i) {
    const float x = left[i];
    lp += kBassCutoff * (x - lp);
    in[i] = x - lp;
    left[i] = kEnsembleDry * x + kEnsembleWet * wet_l[i];
    right[i] = kEnsembleDry * x + kEnsembleWet * wet_r[i];
  }
  filter_state_[0] = lp;
  delay_.Write(in, size);
}

void DuoStereo::Limit(float pre_gain, float* left, float* right, size_t size) {
  float peak = peak_;
  for (size_t i = 0; i < size; ++i) {
    const float l = left[i] * pre_gain;
    const float r = right[i] * pre_gain;
    const float level = max(fabsf(l), fabsf(r));
    if (level > peak) {
      peak = level;
    } else {
      peak += (level - peak) * 0.00002f;
    }
    const float gain = (peak <= 1.0f ? 1.0f : 1.0f / peak) * 0.8f;
    left[i] = l * gain;
    right[i] = r * gain;
  }
  peak_ = peak;
}

}  // namespace plaits
