#!/usr/bin/env python3
#
# Copyright 2026 Andreas Jansson. MIT license (see LICENSE).
#
# Splits a WAV file into two WAV files of half its length (the web demo
# publishes the firmware update file in two parts, and joins them again when
# it is downloaded: see web/index.html).
#
# Usage: split_wav.py IN.wav OUT1.wav OUT2.wav

import struct
import sys


def main():
  source, first, second = sys.argv[1:4]
  data = open(source, 'rb').read()
  if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
    sys.exit('%s: not a WAV file' % source)
  # The chunks: keep the format, split the samples.
  position, fmt, samples = 12, None, None
  while position + 8 <= len(data):
    tag, size = data[position:position + 4], struct.unpack(
        '<I', data[position + 4:position + 8])[0]
    body = data[position + 8:position + 8 + size]
    if tag == b'fmt ':
      fmt = body
    elif tag == b'data':
      samples = body
    position += 8 + size + (size & 1)
  block_align = struct.unpack('<H', fmt[12:14])[0]
  half = len(samples) // block_align // 2 * block_align
  for path, part in ((first, samples[:half]), (second, samples[half:])):
    with open(path, 'wb') as f:
      f.write(b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(part)))
      f.write(b'WAVEfmt ' + struct.pack('<I', len(fmt)) + fmt)
      f.write(b'data' + struct.pack('<I', len(part)) + part)


if __name__ == '__main__':
  main()
