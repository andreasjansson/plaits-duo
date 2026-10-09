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
# Host builds of the test tools (any recent clang or gcc):
#
# - plaits-sim: this firmware, running on the computer (see
#   plaits/test/plaits_sim.cc).
# - plaits-sim-v1.1: the same simulator, built with the unmodified source of
#   the original v1.1 firmware (the upstream-v1.1 tag), as a reference.
# - plaits-analyze: measurements on the audio files written by the simulator.
# - plaits-update-decoder: decodes a firmware update file with the code of the
#   audio bootloader (see plaits/test/update_decoder.cc).
# - plaits-stereo-linearity: checks that nothing clips inside the stereo
#   effects of duo mode (see plaits/test/stereo_linearity.cc).
#
# make -f plaits/test/host.mk

CXX ?= c++
BUILD_DIR = build/host/
UPSTREAM_TAG = upstream-v1.1
UPSTREAM_DIR = $(BUILD_DIR)upstream/

# No FMA contraction, so that the results do not depend on the host CPU.
CXXFLAGS = -DTEST -O2 -g -std=gnu++98 -ffp-contract=off -Wall \
	-Wno-unused-variable -Wno-unused-local-typedef -Wno-unused-private-field

DSP_DIRS = plaits/dsp plaits/dsp/duo plaits/dsp/engine \
	plaits/dsp/physical_modelling plaits/dsp/speech
DSP_SOURCES = $(wildcard $(patsubst %,%/*.cc,$(DSP_DIRS))) \
	plaits/resources.cc stmlib/dsp/units.cc stmlib/utils/random.cc
SIM_SOURCES = plaits/test/plaits_sim.cc plaits/test/sim/sim_hardware.cc
HEADERS = $(wildcard plaits/*.h plaits/dsp/*.h plaits/dsp/*/*.h \
	plaits/test/sim/*.h plaits/test/sim/*/*/*.h)

all: $(BUILD_DIR)plaits-sim $(BUILD_DIR)plaits-sim-v1.1 \
	$(BUILD_DIR)plaits-analyze $(BUILD_DIR)plaits-update-decoder \
	$(BUILD_DIR)plaits-stereo-linearity

$(BUILD_DIR)plaits-sim: $(SIM_SOURCES) plaits/ui.cc plaits/settings.cc \
		$(DSP_SOURCES) $(HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Werror -Iplaits/test/sim -I. -o $@ \
		$(SIM_SOURCES) plaits/ui.cc plaits/settings.cc $(DSP_SOURCES)

$(UPSTREAM_DIR)plaits/ui.cc:
	mkdir -p $(UPSTREAM_DIR)
	git archive $(UPSTREAM_TAG) plaits | tar -x -C $(UPSTREAM_DIR)

# The upstream sources come before the current ones in the include path, the
# stub drivers before both.
$(BUILD_DIR)plaits-sim-v1.1: $(UPSTREAM_DIR)plaits/ui.cc $(SIM_SOURCES) \
		$(wildcard plaits/test/sim/*.h plaits/test/sim/*/*/*.h)
	$(CXX) $(CXXFLAGS) -DPLAITS_SIM_UPSTREAM -Iplaits/test/sim \
		-I$(UPSTREAM_DIR) -I. -o $@ $(SIM_SOURCES) \
		$(UPSTREAM_DIR)plaits/ui.cc $(UPSTREAM_DIR)plaits/settings.cc \
		$(filter-out $(UPSTREAM_DIR)plaits/dsp/duo%, \
			$(wildcard $(patsubst %,$(UPSTREAM_DIR)%/*.cc,$(DSP_DIRS)))) \
		$(UPSTREAM_DIR)plaits/resources.cc stmlib/dsp/units.cc \
		stmlib/utils/random.cc

$(BUILD_DIR)plaits-analyze: plaits/test/plaits_analyze.cc
	mkdir -p $(BUILD_DIR)
	$(CXX) -O2 -Wall -Werror -o $@ $<

$(BUILD_DIR)plaits-stereo-linearity: plaits/test/stereo_linearity.cc \
		plaits/dsp/duo/duo_stereo.cc plaits/resources.cc $(HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Werror -I. -o $@ $(filter %.cc,$^)

$(BUILD_DIR)plaits-update-decoder: plaits/test/update_decoder.cc \
		stm_audio_bootloader/qpsk/demodulator.cc \
		stm_audio_bootloader/qpsk/packet_decoder.cc
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Werror -I. -o $@ $^

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
