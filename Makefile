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
# make test       Builds the test tools with the host compiler, and runs the
#                 tests (cctr 0.31 or later). With TOOLCHAIN_PATH set to an ARM
#                 toolchain (the directory containing bin/arm-none-eabi-gcc),
#                 also builds the firmware and checks its memory use, its CPU
#                 use (with PYTHON having the unicorn, capstone and pyelftools
#                 modules) and its update file (with numpy).
# make firmware   build/plaits/plaits.bin and plaits.hex.
# make wav        build/plaits/plaits.wav, the firmware update file (numpy).
# make bench      The CPU use of each model (see plaits/test/bench.mk).

CCTR ?= cctr
PYTHON ?= python3
TOOLCHAIN_PATH ?=
export TOOLCHAIN_PATH PYTHON

test:
	$(MAKE) -f plaits/test/host.mk
	PATH="$(CURDIR)/build/host:$$PATH" $(CCTR) test/

firmware:
	$(MAKE) -f plaits/makefile bin hex

wav:
	$(MAKE) -f plaits/makefile wav

bench:
	$(MAKE) -f plaits/test/bench.mk

clean:
	rm -rf build/

.PHONY: test firmware wav bench clean
