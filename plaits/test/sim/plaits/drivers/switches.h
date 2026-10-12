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
// Simulator version of plaits/drivers/switches.h.

#ifndef PLAITS_DRIVERS_SWITCHES_H_
#define PLAITS_DRIVERS_SWITCHES_H_

#include "stmlib/stmlib.h"

#include "plaits/test/sim/sim_hardware.h"

#ifdef PLAITS_RIG
#include "plaits/rig/rig.h"
#endif  // PLAITS_RIG

namespace plaits {

enum Switch {
  SWITCH_ROW_1,
  SWITCH_ROW_2,
  SWITCH_LAST
};
  
class Switches {
 public:
  Switches() { }
  ~Switches() { }
  
  void Init() {
    for (int i = 0; i < SWITCH_LAST; ++i) {
      switch_state_[i] = 0xff;
    }
  }
  
  // Same as on the module: the GPIO reads 0 when the button is pressed.
  void Debounce() {
    for (int i = 0; i < SWITCH_LAST; ++i) {
      switch_state_[i] = (switch_state_[i] << 1) | \
          (sim::hardware.switch_down[i] ? 0 : 1);
#ifdef PLAITS_RIG
      if (rig.button_forced(i)) {
        switch_state_[i] &= ~1;
      }
#endif  // PLAITS_RIG
    }
  }
  
  inline bool released(Switch s) const {
    return switch_state_[s] == 0x7f;
  }
  
  inline bool just_pressed(Switch s) const {
    return switch_state_[s] == 0x80;
  }

  inline bool pressed(Switch s) const {
    return switch_state_[s] == 0x00;
  }
  
  inline bool pressed_immediate(Switch s) const {
    return sim::hardware.switch_down[s];
  }
  
 private:
  uint8_t switch_state_[SWITCH_LAST];
  
  DISALLOW_COPY_AND_ASSIGN(Switches);
};

}  // namespace plaits

#endif  // PLAITS_DRIVERS_SWITCHES_H_
