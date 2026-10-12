// Copyright 2016 Emilie Gillet, 2026 Andreas Jansson.
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
// The updater of the rig firmware: the bootloader (plaits/bootloader), linked
// at the top of the flash memory, which the rig firmware starts when it
// receives the command (plaits/rig/rig.h), so that the rig can update it
// without the buttons. It receives the same update files, on MODEL, with the
// same code as the bootloader, and writes them from 0x08008000, never at its
// own address (the update files of the rig firmware are smaller). The
// bootloader is never written, and still updates the module as usual.
//
// When it receives the first page, it writes a vector table which starts the
// updater in place of the first page of the firmware, which it writes last:
// if the update is interrupted, the module starts the updater, which waits
// for an update. If it receives nothing for 30s while the firmware is
// complete, or the left button is pressed, it starts the firmware.
//
// OUT carries the signal received (as with the bootloader), and AUX frames of
// the updater's state, in the same code as the telemetry of the rig firmware
// (rig/telemetry.h): see UpdaterStatus, and rig/host/telemetry.py. The main
// loop renders them: the audio interrupt only copies them, so that it is as
// light as the bootloader's (the demodulator runs in the main loop, and has
// to keep up with the samples).
//
// After an error, it waits for 0.5s of silence before it receives again: a
// reception started in the middle of an update would write it at the wrong
// place.

#include <stm32f37x_conf.h>
#include <cstring>

#include "stmlib/system/bootloader_utils.h"
#include "stmlib/system/flash_programming.h"
#include "stmlib/system/system_clock.h"

#include "stm_audio_bootloader/qpsk/packet_decoder.h"
#include "stm_audio_bootloader/qpsk/demodulator.h"

#include "plaits/drivers/audio_dac.h"
#include "plaits/drivers/firmware_update_adc.h"
#include "plaits/drivers/leds.h"
#include "plaits/drivers/switches.h"

extern "C" {

void NMI_Handler() { }
void HardFault_Handler() { while (1); }
void MemManage_Handler() { while (1); }
void BusFault_Handler() { while (1); }
void UsageFault_Handler() { while (1); }
void SVC_Handler() { }
void DebugMon_Handler() { }
void PendSV_Handler() { }

// From the linker script.
extern uint32_t _estack;
void Reset_Handler();

}

using namespace plaits;
using namespace std;
using namespace stm_audio_bootloader;
using namespace stmlib;

const double kSampleRate = 48000.0;
const double kModulationRate = 6000.0;
const double kBitRate = 12000.0;
const uint32_t kStartAddress = 0x08008000;
const uint32_t kUpdaterAddress = 0x0803c000;
const uint16_t kPacketsPerPage = PAGE_SIZE / kPacketSize;
const uint32_t kTimeout = 30000;  // ms.

enum UiState {
  UI_STATE_WAITING,
  UI_STATE_RECEIVING,
  UI_STATE_ERROR,
  UI_STATE_WRITING,
  UI_STATE_DONE
};

enum UpdaterError {
  UPDATER_ERROR_NONE,
  UPDATER_ERROR_OVERFLOW,  // The demodulator did not keep up.
  UPDATER_ERROR_CRC,
  UPDATER_ERROR_SYNC,
  UPDATER_ERROR_TOO_LARGE,  // The firmware would overwrite the updater.
  UPDATER_ERROR_NO_DATA  // The end of an update without data.
};

// rig/host/telemetry.py reads the same fields, in the same order.
struct UpdaterStatus {
  uint32_t magic;  // PLUP.
  uint32_t version_size;  // Version << 16 | number of words.
  uint32_t state;  // UiState.
  uint32_t packets;  // Received since the start of the update.
  uint32_t pages;  // Written.
  uint32_t errors;  // Updates started again after an error.
  uint32_t peak;  // Of the signal received.
  uint32_t last_error;  // UpdaterError.
  uint32_t demodulator_state;  // At the last error.
  uint32_t checksum;  // FNV-1a of the words before it.
};

const uint32_t kStatusMagic = 0x504c5550;
const uint32_t kStatusVersion = 2;
const int kStatusWords = sizeof(UpdaterStatus) / 4;
const int kStatusPreambleBits = 32;
const int kStatusSamplesPerBit = 6;
const int kStatusSamples = \
    (kStatusPreambleBits + 32 * kStatusWords) * kStatusSamplesPerBit;
const int kStatusPeriod = kStatusSamples + 2400;  // 50ms between frames.
const int16_t kStatusAmplitude = 12000;

// Quiet: samples below this level, for this long (0.5s).
const int32_t kQuietLevel = 1000;
const int32_t kQuietSamples = 24000;

AudioDac audio_dac;
FirmwareUpdateAdc adc;
Leds leds;
Switches switches;
PacketDecoder decoder;
Demodulator demodulator;

int discard_samples = 8000;
int32_t peak = 0;
uint32_t current_address;
uint16_t packet_index;
uint8_t rx_buffer[PAGE_SIZE];
uint8_t first_page[PAGE_SIZE];
bool firmware_complete;

UpdaterStatus status;
int16_t status_samples[kStatusSamples];
volatile int status_position;
volatile bool status_due;
volatile int32_t quiet_samples;

volatile bool switch_pressed = false;
volatile UiState ui_state;

inline void UpdateLeds() {
  leds.Clear();

  // Show bargraph on the upper 4 LEDs.
  int32_t pwm = system_clock.milliseconds() & 15;
  leds.set(3, (peak >> 9) > pwm ? LED_COLOR_GREEN : 0);
  leds.set(2, ((peak - 8192) >> 9) >= pwm ? LED_COLOR_GREEN : 0);
  leds.set(1, ((peak - 16384) >> 9) >= pwm ? LED_COLOR_YELLOW : 0);
  leds.set(0, ((peak - 16384 - 8192) >> 9) >= pwm ? LED_COLOR_RED : 0);

  // Show status info on the lower 4 LEDs: as the bootloader, but red while
  // waiting (to tell the updater from the bootloader).
  switch (ui_state) {
    case UI_STATE_WAITING:
      {
        bool on = system_clock.milliseconds() & 128;
        for (int i = 4; i < 8; ++i) {
          leds.set(i, on ? LED_COLOR_RED : LED_COLOR_OFF);
        }
      }
      break;

    case UI_STATE_RECEIVING:
      {
        int stage = (system_clock.milliseconds() >> 7) & 3;
        leds.set(stage + 4, LED_COLOR_GREEN);
      }
      break;

    case UI_STATE_ERROR:
      {
        bool on = system_clock.milliseconds() & 256;
        for (int i = 0; i < 8; ++i) {
          leds.set(i, on ? LED_COLOR_RED : LED_COLOR_OFF);
        }
      }
      break;

    case UI_STATE_WRITING:
    case UI_STATE_DONE:
      {
        for (uint8_t i = 4; i < 8; ++i) {
          leds.set(i, LED_COLOR_GREEN);
        }
      }
      break;
    }
  leds.Write();
}

extern "C" {

void SysTick_Handler() {
  // The watchdog which the rig firmware started (it cannot be stopped): the
  // module would restart after 1.6s.
  IWDG_ReloadCounter();
  system_clock.Tick();
  switches.Debounce();
  if (switches.just_pressed(Switch(0))) {
    switch_pressed = true;
  }
  UpdateLeds();
}

}

void ErasePage(uint32_t address) {
  FLASH_Unlock();
  FLASH_ErasePage(address);
}

void ProgramPage(uint32_t address, const uint8_t* data, size_t size) {
  ErasePage(address);
  const uint32_t* words = static_cast<const uint32_t*>(
      static_cast<const void*>(data));
  for (size_t written = 0; written < size; written += 4) {
    FLASH_ProgramWord(address, *words++);
    address += 4;
  }
  ++status.pages;
}

// In place of the first page of the firmware, until it is written: a vector
// table which starts the updater.
void ProgramUpdaterVectors() {
  ErasePage(kStartAddress);
  FLASH_ProgramWord(kStartAddress, reinterpret_cast<uint32_t>(&_estack));
  FLASH_ProgramWord(
      kStartAddress + 4, reinterpret_cast<uint32_t>(&Reset_Handler));
}

// Whether the firmware's vector table starts it (not the updater).
bool FirmwareComplete() {
  const uint32_t reset = *reinterpret_cast<const uint32_t*>(kStartAddress + 4);
  return reset >= kStartAddress && reset < kUpdaterAddress;
}

// The samples of the next status frame (in the main loop, while the
// interrupt is between two frames).
void RenderStatus() {
  status.state = ui_state;
  status.peak = peak;
  const uint32_t* words = reinterpret_cast<const uint32_t*>(&status);
  uint32_t hash = 2166136261u;
  for (int i = 0; i < kStatusWords - 1; ++i) {
    hash = (hash ^ words[i]) * 16777619u;
  }
  status.checksum = hash;
  int16_t* s = status_samples;
  for (int bit = -kStatusPreambleBits; bit < 32 * kStatusWords; ++bit) {
    const bool one = bit >= 0 && ((words[bit >> 5] >> (31 - (bit & 31))) & 1);
    for (int i = 0; i < kStatusSamplesPerBit; ++i) {
      const bool second_half = i >= kStatusSamplesPerBit / 2;
      *s++ = one != second_half ? kStatusAmplitude : -kStatusAmplitude;
    }
  }
}

inline void PollStatus() {
  if (status_due) {
    RenderStatus();
    status_due = false;
  }
}

void FillBuffer(AudioDac::Frame* output, size_t size) {
  // A fixed gain (the bootloader's, with its knob at 12 o'clock): the rig
  // sets the level of the update file.
  int32_t sample = 32768 - static_cast<int32_t>(adc.sample());
  adc.Convert();
  sample = sample * 384 >> 8;
  CONSTRAIN(sample, -32767, 32767)

  int32_t rect = abs(sample);
  peak = rect > peak ? rect : (rect + 32767 * peak) >> 15;
  quiet_samples = rect < kQuietLevel ? quiet_samples + 1 : 0;

  if (!discard_samples) {
    demodulator.PushSample(2048 + (sample >> 4));
  } else {
    --discard_samples;
  }
  output->l = -sample;
  int position = status_position;
  output->r = position < kStatusSamples ? status_samples[position] : 0;
  if (++position == kStatusSamples) {
    status_due = true;
  } else if (position == kStatusPeriod) {
    position = 0;
  }
  status_position = position;
}

void InitializeReception() {
  decoder.Init(1000, true);
  demodulator.Init(
      kModulationRate / kSampleRate * 4294967296.0,
      kSampleRate / kModulationRate,
      2.0 * kSampleRate / kBitRate);
  demodulator.SyncCarrier(true);
  decoder.Reset();
  current_address = kStartAddress;
  packet_index = 0;
  status.packets = 0;
  ui_state = UI_STATE_WAITING;
}

void Init() {
  IWDG_ReloadCounter();
  // The vectors of the updater, not of the bootloader.
  NVIC_SetVectorTable(NVIC_VectTab_FLASH, kUpdaterAddress - 0x08000000);
  memset(&status, 0, sizeof(status));
  status.magic = kStatusMagic;
  status.version_size = (kStatusVersion << 16) | kStatusWords;
  RenderStatus();
  status_position = 0;
  status_due = false;
  quiet_samples = 0;
  adc.Init();
  leds.Init();
  switches.Init();
  audio_dac.Init(48000, 1);
  audio_dac.Start(&FillBuffer);
  SysTick_Config(F_CPU / 1000);
}

int main(void) {
  Init();
  InitializeReception();
  firmware_complete = FirmwareComplete();

  bool exit_updater = false;
  while (!exit_updater) {
    UpdaterError error = UPDATER_ERROR_NONE;
    PollStatus();

    if (demodulator.state() == DEMODULATOR_STATE_OVERFLOW) {
      error = UPDATER_ERROR_OVERFLOW;
    } else {
      demodulator.ProcessAtLeast(32);
    }

    if (ui_state == UI_STATE_WAITING && firmware_complete && \
        (system_clock.milliseconds() > kTimeout || switch_pressed)) {
      exit_updater = true;
    }

    while (demodulator.available() && !error && !exit_updater) {
      uint8_t symbol = demodulator.NextSymbol();
      PacketDecoderState state = decoder.ProcessSymbol(symbol);
      switch (state) {
        case PACKET_DECODER_STATE_OK:
          {
            ui_state = UI_STATE_RECEIVING;
            memcpy(
                rx_buffer + (packet_index % kPacketsPerPage) * kPacketSize,
                decoder.packet_data(),
                kPacketSize);
            ++packet_index;
            ++status.packets;
            if ((packet_index % kPacketsPerPage) == 0) {
              ui_state = UI_STATE_WRITING;
              if (current_address >= kUpdaterAddress) {
                // Too large for the updater (the release firmware): this
                // has to be done with the bootloader.
                error = UPDATER_ERROR_TOO_LARGE;
                break;
              } else if (current_address == kStartAddress) {
                memcpy(first_page, rx_buffer, PAGE_SIZE);
                ProgramUpdaterVectors();
                firmware_complete = false;
              } else {
                ProgramPage(current_address, rx_buffer, PAGE_SIZE);
              }
              current_address += PAGE_SIZE;
              decoder.Reset();
              demodulator.SyncCarrier(false);
              ui_state = UI_STATE_RECEIVING;
            } else {
              decoder.Reset();
              demodulator.SyncDecision();
            }
          }
          break;
        case PACKET_DECODER_STATE_ERROR_CRC:
          error = UPDATER_ERROR_CRC;
          break;
        case PACKET_DECODER_STATE_ERROR_SYNC:
          error = UPDATER_ERROR_SYNC;
          break;
        case PACKET_DECODER_STATE_END_OF_TRANSMISSION:
          if (current_address > kStartAddress) {
            ui_state = UI_STATE_WRITING;
            ProgramPage(kStartAddress, first_page, PAGE_SIZE);
            ui_state = UI_STATE_DONE;
            exit_updater = true;
          } else {
            error = UPDATER_ERROR_NO_DATA;
          }
          break;
        default:
          break;
      }
    }

    if (error) {
      // At least a second (the rig sees the error on AUX), then 0.5s of
      // silence (the end of the update), then waits for the update again.
      ui_state = UI_STATE_ERROR;
      ++status.errors;
      status.last_error = error;
      status.demodulator_state = demodulator.state();
      const uint32_t until = system_clock.milliseconds() + 1000;
      while (system_clock.milliseconds() < until || \
             quiet_samples < kQuietSamples) {
        PollStatus();
      }
      InitializeReception();
    }
  }

  // Lets the rig see the last state (DONE) on AUX.
  const uint32_t until = system_clock.milliseconds() + 200;
  while (system_clock.milliseconds() < until) {
    PollStatus();
  }

  adc.DeInit();
  audio_dac.Stop();
  Uninitialize();
  JumpTo(kStartAddress);
  while (1) { }
}
