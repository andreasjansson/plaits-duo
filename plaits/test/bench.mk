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
# CPU use of the firmware, measured on an emulated Cortex-M4 (see
# plaits/test/bench/emulator.py, which needs the unicorn, capstone and
# pyelftools Python modules):
#
# - build/bench/duo.json: the models of duo mode.
# - build/bench/original.json: the models of the original firmware, built from
#   this source.
# - build/bench/upstream.json: the same, built from the source of the original
#   firmware (the upstream-v1.1 tag), with the flags of its own makefile. Its
#   most expensive block is the CPU budget.
#
# The DSP code is compiled exactly as in the firmware: this makefile includes
# plaits/makefile, and the benchmarks are linked with its object files.
#
# make -f plaits/test/bench.mk TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain/

include plaits/makefile

.DEFAULT_GOAL := bench

PYTHON ?= python3
BENCH_DIR = $(BUILD_ROOT)bench/
BENCH_SRC = plaits/test/bench/
UPSTREAM_TAG = upstream-v1.1
UPSTREAM_DIR = $(BENCH_DIR)upstream/

DSP_DIRS = plaits/dsp plaits/dsp/duo plaits/dsp/engine \
	plaits/dsp/physical_modelling plaits/dsp/speech stmlib/dsp
DSP_SOURCES = $(wildcard $(patsubst %,%/*.cc,$(DSP_DIRS))) \
	stmlib/utils/random.cc plaits/resources.cc
DSP_OBJECTS = $(addprefix $(BUILD_DIR),$(notdir $(DSP_SOURCES:.cc=.o)))

BENCH_LDFLAGS = -nostartfiles -Wl,--gc-sections -T $(BENCH_SRC)bench.ld

# (With the dependencies on the headers, which describe the layout of the
# objects of the firmware.)
$(BENCH_DIR)%.o: $(BENCH_SRC)%.cc
	mkdir -p $(BENCH_DIR)
	$(CXX) -c $(CCFLAGS) $(CPPFLAGS) -MMD -MP $< -o $@

-include $(wildcard $(BENCH_DIR)*.d)

$(BENCH_DIR)duo.elf: $(BENCH_DIR)startup.o $(BENCH_DIR)bench_duo.o \
		$(DSP_OBJECTS) $(BENCH_SRC)bench.ld
	$(CXX) $(CCFLAGS) $(BENCH_LDFLAGS) $(filter %.o,$^) -o $@ -lm

$(BENCH_DIR)original.elf: $(BENCH_DIR)startup.o $(BENCH_DIR)bench_original.o \
		$(DSP_OBJECTS) $(BENCH_SRC)bench.ld
	$(CXX) $(CCFLAGS) $(BENCH_LDFLAGS) $(filter %.o,$^) -o $@ -lm

# The upstream source, with the stmlib of this tree. Its makefile has no
# flags of its own (the C++ standard is the default of the compiler it was
# built with).
$(UPSTREAM_DIR)plaits/makefile:
	mkdir -p $(UPSTREAM_DIR)
	git archive $(UPSTREAM_TAG) plaits | tar -x -C $(UPSTREAM_DIR)

$(BENCH_DIR)upstream.elf: $(UPSTREAM_DIR)plaits/makefile \
		$(BENCH_SRC)startup.cc $(BENCH_SRC)bench_original.cc \
		$(BENCH_SRC)bench.ld
	$(CXX) -I$(UPSTREAM_DIR) $(CCFLAGS) -fno-exceptions -fno-rtti \
		-std=gnu++98 $(BENCH_LDFLAGS) \
		$(BENCH_SRC)startup.cc $(BENCH_SRC)bench_original.cc \
		$(UPSTREAM_DIR)plaits/dsp/*.cc $(UPSTREAM_DIR)plaits/dsp/*/*.cc \
		$(UPSTREAM_DIR)plaits/resources.cc \
		stmlib/dsp/*.cc stmlib/utils/random.cc -o $@ -lm

$(BENCH_DIR)%.json: $(BENCH_DIR)%.elf $(BENCH_SRC)emulator.py
	$(PYTHON) $(BENCH_SRC)emulator.py $< $@

bench: $(BENCH_DIR)duo.json $(BENCH_DIR)original.json \
		$(BENCH_DIR)upstream.json
	$(PYTHON) $(BENCH_SRC)report.py $(BENCH_DIR)

.PHONY: bench
