#!/usr/bin/env python3
#
# Copyright 2013 Emilie Gillet.
#
# Author: Emilie Gillet (emilie.o.gillet@gmail.com)
#
# Python 3 version: Copyright 2026 Andreas Jansson.
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
# QPSK encoder for converting firmware .bin files into .wav files, which the
# audio bootloader of the module decodes (for the STM32F1 and STM32F3
# targets).
#
# A Python 3 version of stm_audio_bootloader/qpsk/encoder.py (with the .wav
# writer of stm_audio_bootloader/audio_stream_writer.py), which only runs on
# Python 2. The output is the same: in particular, the divisions which were
# integer divisions in Python 2 are still integer divisions.
#
# Usage: qpsk_encoder.py [-k] [-s SAMPLE_RATE] [-c CARRIER_FREQUENCY]
#            [-b BAUD_RATE] [-p PACKET_SIZE] [-t stm32f1|stm32f3]
#            [-o OUTPUT.wav] FIRMWARE.bin

import optparse
import struct
import sys
import zlib

import numpy


class Scrambler(object):

  def __init__(self, seed=0):
    self._state = seed

  def scramble(self, data):
    scrambled = bytearray(data)
    for index in range(len(scrambled)):
      scrambled[index] ^= self._state >> 24
      self._state = (self._state * 1664525 + 1013904223) & 0xffffffff
    return bytes(scrambled)


class QpskEncoder(object):

  def __init__(
      self,
      sample_rate=48000,
      carrier_frequency=2000,
      bit_rate=8000,
      packet_size=256,
      scramble=False):
    period = sample_rate // carrier_frequency
    symbol_time = sample_rate // (bit_rate // 2)

    assert (symbol_time % period == 0) or (period % symbol_time == 0)
    assert (sample_rate % carrier_frequency) == 0
    assert (sample_rate % bit_rate) == 0

    self._sr = sample_rate
    self._br = bit_rate
    self._carrier_frequency = carrier_frequency
    self._sample_index = 0
    self._packet_size = packet_size
    self._scrambler = Scrambler(0) if scramble else None

  @staticmethod
  def _upsample(x, factor):
    return numpy.tile(x.reshape(len(x), 1), (1, factor)).ravel()

  def _encode_qpsk(self, symbol_stream):
    ratio = self._sr // self._br * 2
    symbol_stream = numpy.array(symbol_stream)
    bitstream_even = 2 * self._upsample(symbol_stream % 2, ratio) - 1
    bitstream_odd = 2 * self._upsample(symbol_stream // 2, ratio) - 1
    return bitstream_even / numpy.sqrt(2.0), bitstream_odd / numpy.sqrt(2.0)

  def _modulate(self, q_mod, i_mod):
    num_samples = len(q_mod)
    t = (numpy.arange(0.0, num_samples) + self._sample_index) / self._sr
    self._sample_index += num_samples
    phase = 2 * numpy.pi * self._carrier_frequency * t
    return (q_mod * numpy.sin(phase) + i_mod * numpy.cos(phase))

  def _encode(self, symbol_stream):
    return self._modulate(*self._encode_qpsk(symbol_stream))

  def _code_blank(self, duration):
    num_zeros = int(duration * self._br / 8) * 4
    symbol_stream = numpy.zeros((num_zeros, 1)).ravel().astype(int)
    return self._encode(symbol_stream)

  def _code_packet(self, data):
    assert len(data) <= self._packet_size
    if len(data) != self._packet_size:
      data = data + b'\xff' * (self._packet_size - len(data))

    if self._scrambler:
      data = self._scrambler.scramble(data)

    crc = zlib.crc32(data) & 0xffffffff

    # 16x 0 for the PLL ; 8x 21 for the edge detector ; 8x 3030 for syncing
    preamble = [0] * 8 + [0x99] * 4 + [0xcc] * 4
    crc_bytes = [crc >> 24, (crc >> 16) & 0xff, (crc >> 8) & 0xff, crc & 0xff]
    packet = preamble + list(data) + crc_bytes

    symbol_stream = []
    for byte in packet:
      symbol_stream.append((byte >> 6) & 0x3)
      symbol_stream.append((byte >> 4) & 0x3)
      symbol_stream.append((byte >> 2) & 0x3)
      symbol_stream.append((byte >> 0) & 0x3)

    return self._encode(symbol_stream)

  def code_intro(self):
    yield numpy.zeros((1 * self._sr, 1)).ravel()
    yield self._code_blank(1.0)

  def code_outro(self, duration=1.0):
    yield self._code_blank(duration)

  def code(self, data, page_size=1024, blank_duration=0.06):
    if len(data) % page_size != 0:
      tail = page_size - (len(data) % page_size)
      data += b'\xff' * tail

    offset = 0
    remaining_bytes = len(data)
    num_packets_written = 0
    while remaining_bytes:
      size = min(remaining_bytes, self._packet_size)
      yield self._code_packet(data[offset:offset+size])
      num_packets_written += 1
      if num_packets_written == page_size // self._packet_size:
        yield self._code_blank(blank_duration)
        num_packets_written = 0
      remaining_bytes -= size
      offset += size


class WavWriter(object):
  """Writes a mono 16-bit .wav file, block by block."""

  def __init__(self, file_name, sample_rate):
    self._f = open(file_name, 'wb')
    self._sample_rate = sample_rate
    self._num_bytes = 0
    self._write_header()

  def _write_header(self):
    current_position = self._f.tell()
    self._f.seek(0)
    self._f.write(b'RIFF')
    self._f.write(struct.pack('<L', 4 + 8 + 16 + 8 + self._num_bytes))
    self._f.write(b'WAVEfmt ')
    self._f.write(struct.pack(
        '<LHHLLHH', 16, 1, 1, self._sample_rate, self._sample_rate * 2, 2,
        16))
    self._f.write(b'data')
    self._f.write(struct.pack('<L', self._num_bytes))
    if current_position:
      self._f.seek(current_position)

  def append(self, signal):
    # Clipped to [-1, 1], scaled and truncated, as in AudioStreamWriter.
    signal = numpy.clip(signal, -1.0, 1.0) * 32767
    samples = numpy.array(signal, dtype='<i2')
    samples.tofile(self._f)
    self._num_bytes += samples.nbytes
    self._write_header()

  def close(self):
    self._f.close()


PAGE_SIZE = { 'stm32f1': 1024, 'stm32f3': 2048 }
PAUSE = { 'stm32f1': 0.06, 'stm32f3': 0.15 }


def main():
  parser = optparse.OptionParser()
  parser.add_option(
      '-k', '--scramble', dest='scramble', action='store_true',
      default=False, help='Randomize data stream')
  parser.add_option(
      '-s', '--sample_rate', dest='sample_rate', type='int', default=48000,
      help='Sample rate in Hz')
  parser.add_option(
      '-c', '--carrier_frequency', dest='carrier_frequency', type='int',
      default=6000, help='Carrier frequency in Hz')
  parser.add_option(
      '-b', '--baud_rate', dest='baud_rate', type='int', default=12000,
      help='Baudrate in bps')
  parser.add_option(
      '-p', '--packet_size', dest='packet_size', type='int', default=256,
      help='Packet size in bytes')
  parser.add_option(
      '-o', '--output_file', dest='output_file', default=None,
      help='Write output file to FILE', metavar='FILE')
  parser.add_option(
      '-t', '--target', dest='target', default='stm32f1',
      help='Set page size and erase time for TARGET', metavar='TARGET')
  options, args = parser.parse_args()

  if len(args) != 1:
    sys.exit('Specify one, and only one firmware .bin file!')
  if options.target not in PAGE_SIZE:
    sys.exit('Unknown target: %s' % options.target)
  with open(args[0], 'rb') as f:
    data = f.read()

  output_file = options.output_file
  if not output_file:
    if '.bin' in args[0]:
      output_file = args[0].replace('.bin', '.wav')
    else:
      output_file = args[0] + '.wav'

  encoder = QpskEncoder(
      options.sample_rate,
      options.carrier_frequency,
      options.baud_rate,
      options.packet_size,
      options.scramble)
  writer = WavWriter(output_file, options.sample_rate)
  for block in encoder.code_intro():
    writer.append(block)
  for block in encoder.code(
      data, PAGE_SIZE[options.target], PAUSE[options.target]):
    if len(block):
      writer.append(block)
  for block in encoder.code_outro(1.0):
    writer.append(block)
  writer.close()


if __name__ == '__main__':
  main()
