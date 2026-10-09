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
// Quadrature stereo coefficients.

#include "plaits/dsp/duo/quadrature_stereo.h"

namespace plaits {

// Squares of the coefficients published by Olli Niemitalo. At 48kHz, the
// phase difference between the two paths stays within 1.5 degrees of 90
// degrees between 20Hz and 22kHz.

/* static */
const float QuadratureStereo::left_coefficients_[kQuadratureNumSections] = {
  0.479400866f, 0.876218494f, 0.976597590f, 0.997499256f
};

/* static */
const float QuadratureStereo::right_coefficients_[kQuadratureNumSections] = {
  0.161758498f, 0.733028932f, 0.945349700f, 0.990599157f
};

}  // namespace plaits
