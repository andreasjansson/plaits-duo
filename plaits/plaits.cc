// Copyright 2016 Emilie Gillet.
// 
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
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

#include <stm32f37x_conf.h>

#include "plaits/drivers/audio_dac.h"
#include "plaits/drivers/debug_pin.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/synthesizer.h"
#include "plaits/settings.h"
#include "plaits/ui.h"
#ifdef PLAITS_RIG
#include "plaits/rig/rig.h"
#include "plaits/rig/telemetry.h"
#endif  // PLAITS_RIG

using namespace plaits;
using namespace stmlib;

// #define PROFILE_INTERRUPT 1

const bool test_adc_noise = false;

AudioDac audio_dac;
Modulations modulations;
Patch patch;
Settings settings;
Ui ui;
Synthesizer synthesizer;
#ifdef PLAITS_RIG
Rig plaits::rig;
Telemetry telemetry;
#endif  // PLAITS_RIG

char shared_buffer[16384];

// Default interrupt handlers.
extern "C" {

void NMI_Handler() { }
void HardFault_Handler() { while (1); }
void MemManage_Handler() { while (1); }
void BusFault_Handler() { while (1); }
void UsageFault_Handler() { while (1); }
void SVC_Handler() { }
void DebugMon_Handler() { }
void PendSV_Handler() { }
void __cxa_pure_virtual() { while (1); }

}

void FillBuffer(AudioDac::Frame* output, size_t size) {
#ifdef PROFILE_INTERRUPT
  TIC
#endif  // PROFILE_INTERRUPT

#ifdef PLAITS_RIG
  const uint32_t start = DWT->CYCCNT;
#endif  // PLAITS_RIG
  IWDG_ReloadCounter();
  
  ui.Poll();
#ifdef PLAITS_RIG
  const uint32_t poll = DWT->CYCCNT;
#endif  // PLAITS_RIG
  
  if (test_adc_noise) {
    static float note_lp = 0.0f;
    float note = modulations.note;
    ONE_POLE(note_lp, note, 0.0001f);
    float cents = (note - note_lp) * 100.0f;
    CONSTRAIN(cents, -8.0f, +8.0f);
    while (size--) {
      output->r = output->l = static_cast<int16_t>(cents * 4040.0f);
      ++output;
    }
  } else {
    synthesizer.Render(patch, modulations, (Voice::Frame*)(output), size);
    ui.set_active_engine(synthesizer.active_engine(patch));
    // While LEVEL is calibrated: the voltages it is calibrated with.
    const int16_t calibration_output = ui.calibration_output();
    if (calibration_output) {
      for (size_t i = 0; i < size; ++i) {
        output[i].l = output[i].r = calibration_output;
      }
    }
  }
  
#ifdef PLAITS_RIG
  rig.Render((Voice::Frame*)(output), size, start);
  telemetry.Process(
      ui, patch, modulations, settings, start, poll, DWT->CYCCNT,
      &output[0].r, 2, size);
#endif  // PLAITS_RIG
  
#ifdef PROFILE_INTERRUPT
  TOC
#endif  // PROFILE_INTERRUPT
}

void Init() {
  NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x8000);
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_16);
  
  BufferAllocator allocator(shared_buffer, 16384);
  synthesizer.Init(&allocator);
  
  volatile size_t counter = 1000000;
  while (counter--);
  
  // The factory test mode of the original firmware (on a module without
  // settings, its test jig sent commands over the serial port of the
  // programming header) is left out, to make room in the flash memory.
  settings.Init();
#ifdef PROFILE_INTERRUPT
  DebugPin::Init();
#endif  // PROFILE_INTERRUPT

#ifdef PLAITS_RIG
  rig.Init(&patch);
#endif  // PLAITS_RIG
  ui.Init(&patch, &modulations, &settings);
#ifdef PLAITS_RIG
  telemetry.Init();
#endif  // PLAITS_RIG
  
  audio_dac.Init(48000, kBlockSize);

  audio_dac.Start(&FillBuffer);
  IWDG_Enable();
}

int main(void) {
  Init();
  while (1) {
#ifdef PLAITS_RIG
    if (rig.update_requested()) {
      rig.StartUpdater();
    } else if (rig.restart_requested()) {
      rig.Restart();
    }
#endif  // PLAITS_RIG
  }
}
