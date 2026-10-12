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
// Timing probes, for the rig firmware only (nothing in the others):
// RIG_PROBE(-1) starts, RIG_PROBE(i) adds the cycles since the previous probe
// to probe i (0 to 7), reported by the telemetry (see Rig::Probe).

#ifndef PLAITS_RIG_PROBE_H_
#define PLAITS_RIG_PROBE_H_

#ifdef PLAITS_RIG
#include "plaits/rig/rig.h"
#define RIG_PROBE(i) plaits::rig.Probe(i)
#else
#define RIG_PROBE(i)
#endif  // PLAITS_RIG

#endif  // PLAITS_RIG_PROBE_H_
