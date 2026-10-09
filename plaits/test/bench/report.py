#!/usr/bin/env python3
#
# Copyright 2026 Andreas Jansson.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
# See http://creativecommons.org/licenses/MIT/ for more information.
#
# -----------------------------------------------------------------------------
#
# Summary of the benchmarks (see bench.mk): the most expensive block of each
# model, compared with the CPU budget (the most expensive block of the
# original firmware, built from its own source).
#
# Usage: report.py BENCH_DIR

import collections
import json
import os
import sys

DUO_MODELS = [
    'classic waveforms', 'waveshaper', '2-operator FM', 'formant',
    'harmonic', 'wavetable', 'string', 'vowel']
ORIGINAL_MODELS = [
    'classic waveforms', 'waveshaper', '2-operator FM', 'formant',
    'harmonic', 'wavetable', 'chords', 'vowel and speech',
    'granular cloud', 'filtered noise', 'particle noise', 'inharmonic string',
    'modal resonator', 'bass drum', 'snare drum', 'hi-hat']


def Load(directory, name):
  with open(os.path.join(directory, name + '.json')) as f:
    results = json.load(f)
  worst = collections.defaultdict(int)
  for tag, cycles in results['blocks'].items():
    group = int(tag) >> 8
    worst[group] = max(worst[group], max(cycles))
  return worst, results['stack']


def Main(directory):
  duo, duo_stack = Load(directory, 'duo')
  original, original_stack = Load(directory, 'original')
  upstream, upstream_stack = Load(directory, 'upstream')
  budget = max(upstream.values())
  budget_model = ORIGINAL_MODELS[max(upstream, key=upstream.get)]
  print('CPU budget: %d cycles per block (the most expensive block of the '
        'original firmware: %s)' % (budget, budget_model))
  print()

  print('Duo mode: most expensive block of each model, in % of the budget')
  print('%-18s %-20s %-20s' % ('', '1 voice', '2 voices'))
  print('%-18s %-9s %-10s %-9s %-10s' % (
      'model', 'stereo', 'split', 'stereo', 'split'))
  duo_max, duo_max_where = 0, None
  for model, name in enumerate(DUO_MODELS):
    row = []
    for voices in (1, 2):
      for layout in (0, 1):
        # Tags: (voices - 1) << 7 | layout << 3 | model (see bench_duo.cc).
        cycles = duo[(voices - 1) << 7 | layout << 3 | model]
        row.append('%3.0f%%' % (100.0 * cycles / budget))
        if cycles > duo_max:
          duo_max = cycles
          duo_max_where = '%s, %d voice%s, %s' % (
              name, voices, 's' if voices == 2 else '',
              'split' if layout else 'stereo')
    print('%-18s %-9s %-10s %-9s %-10s' % tuple([name] + row))
  print()

  print('Original firmware: most expensive block of each model, in cycles')
  print('%-18s %-14s %-14s' % ('model', 'this source', 'v1.1 source'))
  for model, name in enumerate(ORIGINAL_MODELS):
    print('%-18s %-14d %-14d' % (name, original[model], upstream[model]))
  print()

  print('duo mode: at most %.1f%% of the budget (%d cycles, %s)' % (
      100.0 * duo_max / budget, duo_max, duo_max_where))
  original_max = max(original.values())
  print('original firmware: at most %.1f%% of the budget (%d cycles, %s)' % (
      100.0 * original_max / budget, original_max,
      ORIGINAL_MODELS[max(original, key=original.get)]))
  print('stack: at most %d bytes (original firmware: %d bytes, v1.1 source: '
        '%d bytes)' % (duo_stack, original_stack, upstream_stack))

if __name__ == '__main__':
  if len(sys.argv) != 2:
    sys.exit('Usage: %s BENCH_DIR' % sys.argv[0])
  Main(sys.argv[1])
