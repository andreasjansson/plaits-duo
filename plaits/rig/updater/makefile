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
# The updater of the rig firmware (see updater.cc), in build/plaits_updater/:
# the bootloader's code, at the top of the flash memory.
#
# make -f plaits/rig/updater/makefile TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain/

# System specifications
F_CRYSTAL      = 8000000L
F_CPU          = 72000000L
SYSCLOCK       = SYSCLK_FREQ_72MHz
FAMILY         = f37x

# Packages to build
TARGET         = plaits_updater
PACKAGES       = plaits/rig/updater \
		plaits/drivers \
		stm_audio_bootloader/qpsk \
		stmlib/dsp \
		stmlib/utils \
		stmlib/system

TOOLCHAIN_PATH ?= /usr/local/arm-4.8.3/

include stmlib/makefile.inc

# Required with compilers more recent than the gcc 4.8 of the original
# firmware (as for the firmware).
CPPFLAGS += -std=gnu++98

LINKER_SCRIPT = plaits/rig/updater/updater.ld

# Not the library's memmove for the loops which shift the demodulator's
# filters (gcc 4.8, the compiler of the bootloader, did not use it): at 48kHz,
# with the library's memmove, the demodulator needs about all of the
# processor (1000-1300 cycles per sample on the emulated processor, 300-500
# without), and falls behind (plaits/test/bench, rig/host/control.py).
CCFLAGS += -fno-tree-loop-distribute-patterns
