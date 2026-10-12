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
// plaits-sim: runs the firmware (UI, settings and DSP code) on a computer,
// with simulated knobs, buttons, jacks and LEDs, driven by a script read from
// stdin. The MAIN and AUX outputs are written to a stereo WAV file.
//
// Usage: plaits-sim [-o output.wav] < script
//
// Script commands (one per line, # starts a comment). Durations are given
// in ms or s (500ms, 1.5s), voltages in volts, knob positions from 0 to 1.
//
//   knob NAME VALUE      frequency, harmonics, timbre, morph, timbre_cv,
//                        fm_cv, morph_cv (attenuverters: 0.5 is centered).
//   cv JACK VOLTS        Patches a cable carrying VOLTS into JACK: model,
//                        v_oct, fm, harmonics, timbre, morph, trigger, level.
//   patch JACK OUTPUT    Patches OUT (main) or AUX (aux) into JACK.
//   unpatch JACK         Removes the cable.
//   gain_error JACK X    Multiplies the gain of the input stage of JACK by X.
//   hw NAME VALUE        Sets a parameter of the simulated hardware (see
//                        plaits/test/sim/sim_hardware.h): vdda, adc_gain,
//                        swing.
//   press BUTTON         left, right or both.
//   release BUTTON
//   tap BUTTON           Press for 100ms, release, wait 100ms.
//   hold BUTTON TIME     Press for TIME, release, wait 100ms.
//   trigger              Sends a 10ms 5V gate into the trigger jack.
//   wait TIME            Lets the firmware run.
//   leds [TIME]          Prints the LEDs, as seen over TIME (default 200ms):
//                        G (green), R (red), Y (yellow) or . (off) for each
//                        of the 8 LEDs, from top to bottom.
//   duty [TIME]          Prints, for each LED, its color and the share of
//                        TIME (default 200ms) it is lit, in %: Y100 (lit),
//                        Y38 (dimmed by PWM), . (off).
//   trace LED TIME       Prints the color changes of an LED (0 to 7), or of
//                        all the LEDs (trace all TIME).
//   state                Prints the patch and settings.
//   reboot               Power cycle: the RAM is cleared, the settings are
//                        kept in flash.
//   reboot_holding BUTTON  Power cycle with BUTTON held.
//   clear                Discards the audio recorded so far.
//   save PATH            Writes the audio recorded so far to PATH.
//   echo TEXT            Prints TEXT.
//
// LED 0 is the top one (first model of each bank).

#include <cmath>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "plaits/dsp/dsp.h"
#include "stmlib/utils/random.h"
#ifndef PLAITS_SIM_UPSTREAM
#include "plaits/dsp/synthesizer.h"
#else
#include "plaits/dsp/voice.h"
#endif  // PLAITS_SIM_UPSTREAM
#include "plaits/settings.h"
#include "plaits/test/sim/sim_hardware.h"
#include "plaits/ui.h"
#ifdef PLAITS_RIG
#include "plaits/rig/rig.h"
#include "plaits/rig/telemetry.h"
#endif  // PLAITS_RIG

using namespace plaits;
using namespace std;

namespace stmlib {
namespace sim_flash {
Image image;
}  // namespace sim_flash
}  // namespace stmlib

#ifndef PLAITS_SIM_UPSTREAM
Synthesizer synthesizer;
#else
Voice voice;
#endif  // PLAITS_SIM_UPSTREAM
Settings settings;
Ui ui;
#ifdef PLAITS_RIG
// The rig firmware's control and telemetry, without the cycle counts (0).
Rig plaits::rig;
Telemetry telemetry;
#endif  // PLAITS_RIG
Patch patch;
Modulations modulations;
char shared_buffer[16384];

vector<short> recording;
const float kBlockDuration = 1000.0f * kBlockSize / kCorrectedSampleRate;

// As the startup code does on power on: the RAM of the object is cleared,
// and its constructor is called.
template<typename T>
void PowerOn(T* object) {
  memset(static_cast<void*>(object), 0, sizeof(T));
  new (object) T();
}

// Power on, then the same as Init() in plaits.cc. The state of the random
// number generator is also reset (see stmlib/utils/random.cc).
void Boot() {
#ifndef PLAITS_SIM_UPSTREAM
  PowerOn(&synthesizer);
#else
  PowerOn(&voice);
#endif  // PLAITS_SIM_UPSTREAM
  PowerOn(&settings);
  PowerOn(&ui);
  PowerOn(&patch);
  PowerOn(&modulations);
  memset(shared_buffer, 0, sizeof(shared_buffer));
  stmlib::Random::Seed(0x21);
  stmlib::BufferAllocator allocator(shared_buffer, 16384);
#ifndef PLAITS_SIM_UPSTREAM
  synthesizer.Init(&allocator);
#else
  voice.Init(&allocator);
#endif  // PLAITS_SIM_UPSTREAM
  settings.Init();
#ifdef PLAITS_RIG
  PowerOn(&rig);
  rig.Init(&patch);
#endif  // PLAITS_RIG
  ui.Init(&patch, &modulations, &settings);
#ifdef PLAITS_RIG
  PowerOn(&telemetry);
  telemetry.Init();
#endif  // PLAITS_RIG
}

// Same as FillBuffer() in plaits.cc.
void Block() {
  Voice::Frame frames[kBlockSize];
  ui.Poll();
#ifndef PLAITS_SIM_UPSTREAM
  synthesizer.Render(patch, modulations, frames, kBlockSize);
  ui.set_active_engine(synthesizer.active_engine(patch));
  const int16_t calibration_output = ui.calibration_output();
  if (calibration_output) {
    for (size_t i = 0; i < kBlockSize; ++i) {
      frames[i].out = frames[i].aux = calibration_output;
    }
  }
#ifdef PLAITS_RIG
  rig.Render(frames, kBlockSize, 0);
  telemetry.Process(
      ui, patch, modulations, settings, 0, 0, 0, &frames[0].aux, 2,
      kBlockSize);
  if (rig.update_requested() || rig.restart_requested()) {
    // The updater only exists on the module.
    printf("%s requested\n", rig.update_requested() ? "updater" : "restart");
    fflush(stdout);
    rig.Init(&patch);
  }
#endif  // PLAITS_RIG
#else
  voice.Render(patch, modulations, frames, kBlockSize);
  ui.set_active_engine(voice.active_engine());
#endif  // PLAITS_SIM_UPSTREAM
  for (size_t i = 0; i < kBlockSize; ++i) {
    // The output stage of the module inverts the DAC signal.
    recording.push_back(-max(frames[i].out, short(-32767)));
    recording.push_back(-max(frames[i].aux, short(-32767)));
  }
  sim::SetOutputs(frames[kBlockSize - 1].out, frames[kBlockSize - 1].aux);
}

int Blocks(float ms) {
  return static_cast<int>(ms / kBlockDuration + 0.5f);
}

void Run(float ms) {
  for (int i = Blocks(ms); i > 0; --i) {
    Block();
  }
}

bool ParseDuration(const string& s, float* ms) {
  char* end;
  float value = strtof(s.c_str(), &end);
  string unit(end);
  if (end == s.c_str()) {
    return false;
  }
  if (unit == "ms") {
    *ms = value;
  } else if (unit == "s") {
    *ms = value * 1000.0f;
  } else {
    return false;
  }
  return true;
}

int Index(const char* const* names, int n, const string& name) {
  for (int i = 0; i < n; ++i) {
    if (name == names[i]) {
      return i;
    }
  }
  return -1;
}

const char* const kJacks[] = {
  "model", "v_oct", "fm", "harmonics", "timbre", "morph", "trigger", "level"
};
const char* const kKnobs[] = {
  "frequency", "harmonics", "timbre", "morph", "timbre_cv", "fm_cv", "morph_cv"
};

bool SetButtons(const string& name, bool down) {
  if (name == "left" || name == "both") {
    sim::hardware.switch_down[0] = down;
  }
  if (name == "right" || name == "both") {
    sim::hardware.switch_down[1] = down;
  }
  return name == "left" || name == "right" || name == "both";
}

char ColorChar(uint32_t color) {
  switch (color) {
    case LED_COLOR_GREEN: return 'G';
    case LED_COLOR_RED: return 'R';
    case LED_COLOR_YELLOW: return 'Y';
    case LED_COLOR_OFF: return '.';
    default: return '?';
  }
}

void PrintLeds(float ms) {
  int n = max(Blocks(ms), 1);
  // For each LED, the number of blocks during which each color was shown.
  int count[kNumLEDs][4] = { { 0 } };
  const uint32_t colors[4] = {
    LED_COLOR_OFF, LED_COLOR_GREEN, LED_COLOR_RED, LED_COLOR_YELLOW
  };
  for (int b = 0; b < n; ++b) {
    Block();
    for (int i = 0; i < kNumLEDs; ++i) {
      for (int c = 0; c < 4; ++c) {
        if (sim::hardware.led_color[i] == colors[c]) {
          ++count[i][c];
        }
      }
    }
  }
  string s;
  for (int i = 0; i < kNumLEDs; ++i) {
    // The main color: the most frequent one, unless the LED is mostly off.
    int best = 1;
    for (int c = 2; c < 4; ++c) {
      if (count[i][c] > count[i][best]) {
        best = c;
      }
    }
    s += count[i][best] * 4 >= n ? ColorChar(colors[best]) : '.';
  }
  printf("leds %s\n", s.c_str());
}

void PrintDuty(float ms) {
  int n = max(Blocks(ms), 1);
  int count[kNumLEDs][4] = { { 0 } };
  const uint32_t colors[4] = {
    LED_COLOR_OFF, LED_COLOR_GREEN, LED_COLOR_RED, LED_COLOR_YELLOW
  };
  for (int b = 0; b < n; ++b) {
    Block();
    for (int i = 0; i < kNumLEDs; ++i) {
      for (int c = 0; c < 4; ++c) {
        if (sim::hardware.led_color[i] == colors[c]) {
          ++count[i][c];
        }
      }
    }
  }
  string s;
  for (int i = 0; i < kNumLEDs; ++i) {
    int best = 1;
    for (int c = 2; c < 4; ++c) {
      if (count[i][c] > count[i][best]) {
        best = c;
      }
    }
    char text[8];
    if (count[i][best]) {
      snprintf(text, sizeof(text), "%c%d", ColorChar(colors[best]),
               static_cast<int>(100.0f * count[i][best] / n + 0.5f));
    } else {
      snprintf(text, sizeof(text), ".");
    }
    s += (i ? " " : "") + string(text);
  }
  printf("duty %s\n", s.c_str());
}

// led = -1 for all LEDs.
void Trace(int led, float ms) {
  int n = Blocks(ms);
  string previous;
  for (int b = 0; b < n; ++b) {
    Block();
    string current;
    for (int i = 0; i < kNumLEDs; ++i) {
      if (led == -1 || led == i) {
        current += ColorChar(sim::hardware.led_color[i]);
      }
    }
    if (current != previous) {
      printf("%6.0fms %s\n", b * kBlockDuration, current.c_str());
      previous = current;
    }
  }
}

bool SetHardware(const char* name, float value) {
  sim::Hardware& h = sim::hardware;
  float* floats[] = { &h.vdda, &h.adc_gain, &h.swing };
  const char* names[] = { "vdda", "adc_gain", "swing" };
  for (int i = 0; i < 3; ++i) {
    if (!strcmp(name, names[i])) {
      *floats[i] = value;
      return true;
    }
  }
  return false;
}

void PrintState() {
  const State& state = settings.state();
#ifndef PLAITS_SIM_UPSTREAM
  printf("duo_mode %d\n", patch.duo_mode ? 1 : 0);
  printf("engine %d\n", patch.engine);
  printf("duo_engine %d\n", patch.duo_engine);
  printf("saved duo_mode %d engine %d duo_engine %d\n",
         state.duo_mode, state.engine, state.duo_engine);
  const PersistentData& p = settings.persistent_data();
  printf("level calibration scale %.3f offset %.3f\n",
         p.level_note_scale, p.level_note_offset);
#else
  printf("engine %d\n", patch.engine);
  printf("saved engine %d\n", state.engine);
#endif  // PLAITS_SIM_UPSTREAM
  printf("color_blind %d\n", state.color_blind);
}

bool WriteWav(const char* path) {
  FILE* fp = fopen(path, "wb");
  if (!fp) {
    return false;
  }
  uint32_t data_size = recording.size() * sizeof(short);
  uint32_t l;
  uint16_t s;
  fwrite("RIFF", 4, 1, fp);
  l = 36 + data_size; fwrite(&l, 4, 1, fp);
  fwrite("WAVEfmt ", 8, 1, fp);
  l = 16; fwrite(&l, 4, 1, fp);
  s = 1; fwrite(&s, 2, 1, fp);
  s = 2; fwrite(&s, 2, 1, fp);
  // Actual sample rate of the module (see plaits/dsp/dsp.h).
  l = 47872; fwrite(&l, 4, 1, fp);
  l = 47872 * 4; fwrite(&l, 4, 1, fp);
  s = 4; fwrite(&s, 2, 1, fp);
  s = 16; fwrite(&s, 2, 1, fp);
  fwrite("data", 4, 1, fp);
  fwrite(&data_size, 4, 1, fp);
  if (!recording.empty()) {
    fwrite(&recording[0], sizeof(short), recording.size(), fp);
  }
  fclose(fp);
  return true;
}

int main(int argc, char** argv) {
  const char* output = NULL;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-o") && i + 1 < argc) {
      output = argv[++i];
    } else {
      fprintf(stderr, "Usage: %s [-o output.wav] < script\n", argv[0]);
      return 1;
    }
  }

  sim::ResetHardware();
  Boot();

  char line[512];
  int line_number = 0;
  bool ok = true;
  while (ok && fgets(line, sizeof(line), stdin)) {
    ++line_number;
    char* comment = strchr(line, '#');
    if (comment) {
      *comment = '\0';
    }
    char w[4][128];
    int n = sscanf(line, "%127s %127s %127s %127s", w[0], w[1], w[2], w[3]);
    if (n <= 0) {
      continue;
    }
    string command(w[0]);
    float ms = 0.0f;
    int index;
    if (command == "knob" && n == 3 &&
        (index = Index(kKnobs, sim::kNumPots, w[1])) >= 0) {
      sim::hardware.pot[index] = atof(w[2]);
    } else if (command == "cv" && n == 3 &&
               (index = Index(kJacks, sim::kNumCvInputs, w[1])) >= 0) {
      sim::hardware.patched[index] = true;
      sim::hardware.patched_output[index] = 0;
      sim::hardware.cv_volts[index] = atof(w[2]);
    } else if (command == "patch" && n == 3 &&
               (index = Index(kJacks, sim::kNumCvInputs, w[1])) >= 0 &&
               (!strcmp(w[2], "main") || !strcmp(w[2], "aux"))) {
      sim::hardware.patched[index] = true;
      sim::hardware.patched_output[index] = !strcmp(w[2], "main") ? 1 : 2;
    } else if (command == "unpatch" && n == 2 &&
               (index = Index(kJacks, sim::kNumCvInputs, w[1])) >= 0) {
      sim::hardware.patched[index] = false;
      sim::hardware.patched_output[index] = 0;
    } else if (command == "hw" && n == 3 && SetHardware(w[1], atof(w[2]))) {
    } else if (command == "gain_error" && n == 3 &&
               (index = Index(kJacks, sim::kNumCvInputs, w[1])) >= 0) {
      sim::hardware.cv_gain_error[index] = atof(w[2]);
    } else if (command == "press" && n == 2 && SetButtons(w[1], true)) {
    } else if (command == "release" && n == 2 && SetButtons(w[1], false)) {
    } else if (command == "tap" && n == 2 && SetButtons(w[1], true)) {
      Run(100.0f);
      SetButtons(w[1], false);
      Run(100.0f);
    } else if (command == "hold" && n == 3 && ParseDuration(w[2], &ms) &&
               SetButtons(w[1], true)) {
      Run(ms);
      SetButtons(w[1], false);
      Run(100.0f);
    } else if (command == "trigger" && n == 1) {
      const int trigger = 6;
      sim::hardware.patched[trigger] = true;
      sim::hardware.cv_volts[trigger] = 5.0f;
      Run(10.0f);
      sim::hardware.cv_volts[trigger] = 0.0f;
    } else if (command == "wait" && n == 2 && ParseDuration(w[1], &ms)) {
      Run(ms);
    } else if (command == "leds" && n == 1) {
      PrintLeds(200.0f);
    } else if (command == "leds" && n == 2 && ParseDuration(w[1], &ms)) {
      PrintLeds(ms);
    } else if (command == "duty" && n == 1) {
      PrintDuty(200.0f);
    } else if (command == "duty" && n == 2 && ParseDuration(w[1], &ms)) {
      PrintDuty(ms);
    } else if (command == "trace" && n == 3 && ParseDuration(w[2], &ms)) {
      Trace(string(w[1]) == "all" ? -1 : (atoi(w[1]) & 7), ms);
    } else if (command == "state" && n == 1) {
      PrintState();
    } else if (command == "reboot" && n == 1) {
      Boot();
    } else if (command == "reboot_holding" && n == 2 &&
               SetButtons(w[1], true)) {
      Boot();
      SetButtons(w[1], false);
    } else if (command == "clear" && n == 1) {
      recording.clear();
    } else if (command == "save" && n == 2) {
      if (!WriteWav(w[1])) {
        perror(w[1]);
        ok = false;
      }
    } else if (command == "echo") {
      const char* text = line + strlen("echo");
      while (*text == ' ') ++text;
      printf("%s", text);
      if (!strchr(text, '\n')) printf("\n");
    } else {
      fprintf(stderr, "line %d: invalid command: %s", line_number, line);
      ok = false;
    }
    fflush(stdout);
  }

  if (output && !WriteWav(output)) {
    perror(output);
    return 1;
  }
  return ok ? 0 : 1;
}
