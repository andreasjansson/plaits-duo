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
// Stereo effects of duo mode, which turn the mono mix of the two voices into
// a stereo signal (the models which make their own stereo signal don't use
// them). Each model uses the one which suits it best (see DuoVoice::Init):
//
// - Diffuser: the mix reaches the two channels through two different chains
//   of Schroeder allpass filters. Every frequency has the same level in both
//   channels, only the phase differs, by an amount which changes sign many
//   times across the spectrum. The delays inside each chain move slowly, in
//   opposite directions, so that this pattern drifts (and the slight lean of
//   each note averages out) while the total delay of each chain, and thus
//   the group delay at low frequencies, stays the same in both channels: the
//   bass stays in the middle.
// - Chorus: the chorus of the Juno-60, mode I (measured by Andy Harman, see
//   https://github.com/pendragon-andyh/Juno60): a 0.513Hz triangle LFO sweeps
//   two delays between 1.66 and 5.35ms in opposite directions, and each
//   channel is the dry signal plus one of the delayed signals. The
//   difference between the channels is high-passed, so that the bass stays
//   in the middle.
// - Ensemble: the ensemble of the string machine model of Plaits 1.2
//   (plaits/dsp/fx/ensemble.h): slow (0.75Hz) and fast (6.57Hz) LFOs at 0,
//   120 and 240 degrees modulate three taps around a 4ms delay. The original
//   mixes the taps of two delay lines fed with two different signals, and
//   gives the same result in both channels when the two signals are the
//   same. Here, the left channel reads the taps at 4ms + modulation and the
//   right channel at 4ms - modulation. The bass is kept out of the effect.
//
// The three effects share the same memory, since only one model runs at a
// time. The allpass sections of the diffuser store their delay lines as
// 16-bit samples, at half scale (as floats, they would need twice as much
// memory). The delays of the diffuser move so slowly that they are constant
// through each block; the delays of the chorus and of the ensemble move
// linearly through each block.

#ifndef PLAITS_DSP_DUO_DUO_STEREO_H_
#define PLAITS_DSP_DUO_DUO_STEREO_H_

#include "stmlib/stmlib.h"

#include "plaits/dsp/dsp.h"

namespace plaits {

enum DuoStereoEffect {
  DUO_STEREO_EFFECT_NONE,
  DUO_STEREO_EFFECT_DIFFUSER,
  DUO_STEREO_EFFECT_CHORUS,
  DUO_STEREO_EFFECT_ENSEMBLE
};

// Number of samples copied around the ends of a delay line, so that blocks
// can be read across the end of the ring buffer.
const size_t kDuoDelayGuard = kMaxBlockSize + 2;

// A delay line, in a ring buffer which is preceded by a copy of its last
// sample, and followed by a copy of its first kDuoDelayGuard samples.
class DuoDelay {
 public:
  DuoDelay() { }
  ~DuoDelay() { }

  // The memory used by a line of the given length, with the copies.
  static size_t memory_size(size_t length) {
    return length + 1 + kDuoDelayGuard;
  }

  void Init(float* memory, size_t length);

  // Writes a block.
  void Write(const float* in, size_t size);

  // The delay of a tap at the end of the next block, as close to target as
  // possible without crossing a whole number of samples: the two samples
  // between which a tap reads stay the same through a block (the delay
  // reaches the target in the following blocks: it moves by less than a
  // sample per block).
  static float Approach(float delay, float target);

  // Writes the sum of num_taps delayed signals to out, for the block which
  // is about to be written: tap k is delayed by delay[k] samples at the start
  // of the block, and target[k] samples at the end (see Approach). The delays
  // must be longer than the block.
  template<int num_taps>
  void Read(
      const float* delay,
      const float* target,
      float* out,
      size_t size) const;

 private:
  float* begin_;
  float* end_;
  float* write_;

  DISALLOW_COPY_AND_ASSIGN(DuoDelay);
};

// The delay line of an allpass section, read and written one sample at a
// time.
struct DuoAllpassLine {
  int16_t* begin;
  int16_t* end;
  int16_t* write;

  void Init(int16_t* memory, size_t length);

  // The sample written delay samples before the next one.
  inline int16_t* tap(int delay) const {
    int16_t* p = write - delay;
    return p < begin ? p + (end - begin) : p;
  }
};

// Memory of the effects, in floats.
const size_t kDuoStereoMemorySize = 352;

class DuoStereo {
 public:
  DuoStereo() { }
  ~DuoStereo() { }

  void Init();

  // Selects the effect, and clears its memory (and that of the limiter).
  void set_effect(DuoStereoEffect effect);

  // In: the mono mix in left. Out: the left and right channels.
  void Process(float* left, float* right, size_t size);

  // As stmlib::Limiter, with the same gain for both channels, and an
  // instant attack: the output never goes above 0.8 (stmlib::Limiter lets
  // the start of a sudden attack through, which then clips).
  void Limit(float pre_gain, float* left, float* right, size_t size);

 private:
  void ProcessDiffuser(float* left, float* right, size_t size);
  void ProcessChorus(float* left, float* right, size_t size);
  void ProcessEnsemble(float* left, float* right, size_t size);

  // Two allpass sections in series, from in to out (which can be the same).
  void ProcessAllpassChain(
      DuoAllpassLine* a,
      float delay_a,
      DuoAllpassLine* b,
      float delay_b,
      const float* in,
      float* out,
      size_t size);

  DuoStereoEffect effect_;
  union {
    int16_t allpass[2 * kDuoStereoMemorySize];
    float delay[kDuoStereoMemorySize];
  } memory_;
  DuoAllpassLine allpass_[4];
  DuoDelay delay_;

  // LFO phases, in cycles (diffuser and chorus), or sines and cosines
  // (ensemble).
  float phase_[2];
  float lfo_sine_[2];
  float lfo_cosine_[2];
  // One-pole filters.
  float filter_state_[2];
  // Delays of the taps of the chorus and of the ensemble, in the left and
  // right channels.
  float tap_delay_[2][3];
  float peak_;

  DISALLOW_COPY_AND_ASSIGN(DuoStereo);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_STEREO_H_
