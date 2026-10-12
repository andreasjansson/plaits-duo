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
# The web demo, in build/web/: the page (and a page to choose its loops,
# picker.html), its mark, its audio thread and loops, the firmware
# compiled to WebAssembly (with the simulated hardware of plaits-sim) and its
# JavaScript translation (for pages which cannot compile WebAssembly), and
# the firmware update file in two halves, which the page joins again in a
# zip when it is downloaded (the firmware must have been built: make wav).
#
# make -f web/web.mk WASI_SDK_PATH=/path/to/wasi-sdk BINARYEN_PATH=/path/to/binaryen
#
# (wasi-sdk 34 and binaryen 133 were used.)

WASI_SDK_PATH ?=
BINARYEN_PATH ?=
PYTHON ?= python3
BUILD_DIR = build/web/

CXX = $(WASI_SDK_PATH)/bin/clang++
# (wasi-sdk 34 has libc++ with and without exceptions, in eh/ and noeh/, and
# clang searches neither.)
CXXFLAGS = --target=wasm32-wasip1 --sysroot=$(WASI_SDK_PATH)/share/wasi-sysroot \
	-isystem $(WASI_SDK_PATH)/share/wasi-sysroot/include/wasm32-wasip1/noeh/c++/v1 \
	-O3 -DTEST -DBENCH -std=gnu++98 -fno-exceptions -fno-rtti \
	-ffp-contract=off -mexec-model=reactor -Wall -Werror \
	-Wno-unused-variable -Wno-unused-local-typedef -Wno-unused-private-field \
	-Iplaits/test/sim -I.

DSP_DIRS = plaits/dsp plaits/dsp/duo plaits/dsp/engine \
	plaits/dsp/physical_modelling plaits/dsp/speech
SOURCES = web/plaits_web.cc plaits/test/sim/sim_hardware.cc plaits/ui.cc \
	plaits/ui_calibration.cc plaits/settings.cc $(wildcard $(patsubst %,%/*.cc,$(DSP_DIRS))) \
	plaits/resources.cc stmlib/dsp/units.cc stmlib/utils/random.cc
HEADERS = $(wildcard plaits/*.h plaits/dsp/*.h plaits/dsp/*/*.h \
	plaits/test/sim/*.h plaits/test/sim/*/*/*.h)

all: $(BUILD_DIR)index.html $(BUILD_DIR)picker.html $(BUILD_DIR)worklet.js \
	$(BUILD_DIR)loops.js $(BUILD_DIR)plaits.wasm $(BUILD_DIR)plaits_fallback.js \
	$(BUILD_DIR)plaits_duo_firmware_1.wav $(BUILD_DIR)gandaberunda.png

$(BUILD_DIR)plaits.wasm: $(SOURCES) $(HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Wl,--strip-debug -o $@ $(SOURCES)

# A script which defines globalThis.PlaitsFallback, with the same exports as
# the WebAssembly module (an audio worklet can only load scripts).
$(BUILD_DIR)plaits_fallback.js: $(BUILD_DIR)plaits.wasm
	$(BINARYEN_PATH)/bin/wasm2js -O3 $< -o $(BUILD_DIR)plaits_wasm2js.mjs
	sed -e '/^export var /d' $(BUILD_DIR)plaits_wasm2js.mjs > $@
	printf '\nglobalThis.PlaitsFallback = retasmFunc;\n' >> $@
	rm $(BUILD_DIR)plaits_wasm2js.mjs

$(BUILD_DIR)%: web/%
	mkdir -p $(BUILD_DIR)
	cp $< $@

$(BUILD_DIR)plaits_duo_firmware_1.wav: build/plaits/plaits.wav web/split_wav.py
	mkdir -p $(BUILD_DIR)
	$(PYTHON) web/split_wav.py $< $@ $(BUILD_DIR)plaits_duo_firmware_2.wav

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
