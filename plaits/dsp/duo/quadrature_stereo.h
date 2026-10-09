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
// Quadrature ("phase difference") stereo. The left and right channels go
// through two allpass chains whose phase responses are 90 degrees apart over
// the audio band (Olli Niemitalo's polyphase IIR Hilbert transformer). Every
// frequency component therefore reaches the two outputs with the same
// amplitude but with a 90 degree phase difference. Unlike delay-based
// widening, the mono sum has a flat magnitude response: there is no comb
// filtering when the two outputs are summed.
//
// Used for the models which do not have an intrinsic way of producing two
// different channels (string and speech synthesis).

#ifndef PLAITS_DSP_DUO_QUADRATURE_STEREO_H_
#define PLAITS_DSP_DUO_QUADRATURE_STEREO_H_

#include "stmlib/stmlib.h"

#include <algorithm>

namespace plaits {

const int kQuadratureNumSections = 4;

class AllpassChain {
 public:
  AllpassChain() { }
  ~AllpassChain() { }
  
  void Init(const float* coefficients) {
    for (int i = 0; i < kQuadratureNumSections; ++i) {
      a_[i] = coefficients[i];
    }
    for (int i = 0; i <= kQuadratureNumSections; ++i) {
      z1_[i] = z2_[i] = 0.0f;
    }
  }
  
  // Each section is y[n] = a * (x[n] + y[n - 2]) - x[n - 2], and the input
  // of a section is the output of the previous one, so the state consists of
  // the last two values of the input and of each section output. Samples are
  // processed in pairs so that the state never has to be shifted: the first
  // sample of the pair uses (and replaces) the values at n - 2, the second
  // one the values at n - 1. size must be even.
  inline void Process(float* in_out, size_t size) {
    const float a0 = a_[0];
    const float a1 = a_[1];
    const float a2 = a_[2];
    const float a3 = a_[3];
    float p0 = z2_[0], p1 = z2_[1], p2 = z2_[2], p3 = z2_[3], p4 = z2_[4];
    float q0 = z1_[0], q1 = z1_[1], q2 = z1_[2], q3 = z1_[3], q4 = z1_[4];
    for (size_t i = 0; i < size; i += 2) {
      float v0 = in_out[i];
      float v1 = a0 * (v0 + p1) - p0;
      float v2 = a1 * (v1 + p2) - p1;
      float v3 = a2 * (v2 + p3) - p2;
      float v4 = a3 * (v3 + p4) - p3;
      in_out[i] = v4;
      p0 = v0; p1 = v1; p2 = v2; p3 = v3; p4 = v4;
      
      float w0 = in_out[i + 1];
      float w1 = a0 * (w0 + q1) - q0;
      float w2 = a1 * (w1 + q2) - q1;
      float w3 = a2 * (w2 + q3) - q2;
      float w4 = a3 * (w3 + q4) - q3;
      in_out[i + 1] = w4;
      q0 = w0; q1 = w1; q2 = w2; q3 = w3; q4 = w4;
    }
    z2_[0] = p0; z2_[1] = p1; z2_[2] = p2; z2_[3] = p3; z2_[4] = p4;
    z1_[0] = q0; z1_[1] = q1; z1_[2] = q2; z1_[3] = q3; z1_[4] = q4;
  }

 private:
  float a_[kQuadratureNumSections];
  float z1_[kQuadratureNumSections + 1];
  float z2_[kQuadratureNumSections + 1];
  
  DISALLOW_COPY_AND_ASSIGN(AllpassChain);
};

class QuadratureStereo {
 public:
  QuadratureStereo() { }
  ~QuadratureStereo() { }

  void Init() {
    left_.Init(left_coefficients_);
    right_.Init(right_coefficients_);
    delay_ = 0.0f;
  }

  // left = A0(left), right = A90(right), in place. When the same signal is
  // fed to both inputs, the outputs are 90 degrees apart. size must be even.
  inline void Process(float* left, float* right, size_t size) {
    left_.Process(left, size);
    right_.Process(right, size);
    // The left path needs an extra sample of delay.
    for (size_t i = 0; i < size; ++i) {
      const float s = left[i];
      left[i] = delay_;
      delay_ = s;
    }
  }
  
  // Takes the mono signals of the first voice (in left) and of the second
  // voice (in right), places them slightly apart in the stereo field, and
  // applies the quadrature processing. With only one voice, right is
  // ignored and the voice is centered.
  inline void ProcessVoices(
      float* left,
      float* right,
      int num_voices,
      float near_gain,
      float far_gain,
      size_t size) {
    if (num_voices == 1) {
      std::copy(&left[0], &left[size], &right[0]);
    } else {
      for (size_t i = 0; i < size; ++i) {
        const float a = left[i];
        const float b = right[i];
        left[i] = near_gain * a + far_gain * b;
        right[i] = far_gain * a + near_gain * b;
      }
    }
    Process(left, right, size);
  }

 private:
  AllpassChain left_;
  AllpassChain right_;
  float delay_;

  static const float left_coefficients_[kQuadratureNumSections];
  static const float right_coefficients_[kQuadratureNumSections];

  DISALLOW_COPY_AND_ASSIGN(QuadratureStereo);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_QUADRATURE_STEREO_H_
