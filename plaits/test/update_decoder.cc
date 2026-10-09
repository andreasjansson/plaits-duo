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
// plaits-update-decoder: decodes a firmware update file (made by
// tools/qpsk_encoder.py) with the demodulator and the packet decoder of the
// audio bootloader, as the module does (see plaits/bootloader/bootloader.cc),
// and writes the pages it receives - the firmware, padded with 0xff to a
// whole number of pages.
//
// Usage: plaits-update-decoder UPDATE.wav FIRMWARE.bin

#include <cstdio>
#include <cstring>
#include <vector>

#include "stm_audio_bootloader/qpsk/demodulator.h"
#include "stm_audio_bootloader/qpsk/packet_decoder.h"

using namespace stm_audio_bootloader;
using namespace std;

const double kSampleRate = 48000.0;
const double kModulationRate = 6000.0;
const double kBitRate = 12000.0;
const size_t kPageSize = 2048;  // STM32F37x.
const size_t kPacketsPerPage = kPageSize / kPacketSize;

// The samples of a mono 16-bit .wav file (the data chunk only).
bool ReadWav(const char* path, vector<int16_t>* samples) {
  FILE* fp = fopen(path, "rb");
  if (!fp) {
    return false;
  }
  char id[5] = { 0 };
  uint32_t size;
  fseek(fp, 12, SEEK_SET);
  while (fread(id, 1, 4, fp) == 4 && fread(&size, 4, 1, fp) == 1) {
    if (!strcmp(id, "data")) {
      samples->resize(size / 2);
      bool ok = fread(&(*samples)[0], 2, size / 2, fp) == size / 2;
      fclose(fp);
      return ok;
    }
    fseek(fp, size, SEEK_CUR);
  }
  fclose(fp);
  return false;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s UPDATE.wav FIRMWARE.bin\n", argv[0]);
    return 1;
  }
  vector<int16_t> samples;
  if (!ReadWav(argv[1], &samples)) {
    fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }

  // Same initialization as InitializeReception().
  PacketDecoder decoder;
  Demodulator demodulator;
  decoder.Init(1000, true);
  demodulator.Init(
      kModulationRate / kSampleRate * 4294967296.0,
      kSampleRate / kModulationRate,
      2.0 * kSampleRate / kBitRate);
  demodulator.SyncCarrier(true);
  decoder.Reset();

  vector<uint8_t> firmware;
  uint8_t page[kPageSize];
  size_t packet_index = 0;
  bool done = false;
  // The bootloader discards its first 8000 samples.
  for (size_t n = 8000; n < samples.size() && !done; ++n) {
    // Same conversion as FillBuffer(), with a gain of 1.
    demodulator.PushSample(2048 + (samples[n] >> 4));
    if (demodulator.state() == DEMODULATOR_STATE_OVERFLOW) {
      fprintf(stderr, "overflow\n");
      return 1;
    }
    demodulator.ProcessAtLeast(32);
    while (demodulator.available() && !done) {
      PacketDecoderState state = decoder.ProcessSymbol(
          demodulator.NextSymbol());
      if (state == PACKET_DECODER_STATE_OK) {
        memcpy(
            page + (packet_index % kPacketsPerPage) * kPacketSize,
            decoder.packet_data(),
            kPacketSize);
        ++packet_index;
        if (packet_index % kPacketsPerPage == 0) {
          firmware.insert(firmware.end(), page, page + kPageSize);
          decoder.Reset();
          demodulator.SyncCarrier(false);
        } else {
          decoder.Reset();
          demodulator.SyncDecision();
        }
      } else if (state == PACKET_DECODER_STATE_ERROR_CRC ||
                 state == PACKET_DECODER_STATE_ERROR_SYNC) {
        fprintf(stderr, "%s error in packet %d\n",
                state == PACKET_DECODER_STATE_ERROR_CRC ? "CRC" : "sync",
                static_cast<int>(packet_index));
        return 1;
      } else if (state == PACKET_DECODER_STATE_END_OF_TRANSMISSION) {
        done = true;
      }
    }
  }
  if (!done) {
    fprintf(stderr, "no end of transmission\n");
    return 1;
  }
  FILE* fp = fopen(argv[2], "wb");
  if (!fp || fwrite(&firmware[0], 1, firmware.size(), fp) != firmware.size()) {
    fprintf(stderr, "cannot write %s\n", argv[2]);
    return 1;
  }
  fclose(fp);
  printf("%d pages received\n", static_cast<int>(firmware.size() / kPageSize));
  return 0;
}
