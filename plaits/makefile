# Copyright 2016 Emilie Gillet.
# 
# Author: Emilie Gillet (emilie.o.gillet@gmail.com)
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

# System specifications
F_CRYSTAL      = 8000000L
F_CPU          = 72000000L
SYSCLOCK       = SYSCLK_FREQ_72MHz
FAMILY         = f37x
# USB            = enabled

APPLICATION_LARGE = TRUE
BOOTLOADER        = plaits_bootloader

# Preferred upload command
UPLOAD_COMMAND  = upload_combo_jtag_erase_first

# Packages to build
TARGET         = plaits
PACKAGES       = plaits \
		plaits/drivers \
		plaits/dsp \
		plaits/dsp/fx \
		plaits/dsp/drum_modelling \
		plaits/dsp/duo \
		plaits/dsp/engine \
		plaits/dsp/physical_modelling \
		plaits/dsp/speech \
		stmlib/dsp \
		stmlib/utils \
		stmlib/system
RESOURCES      = plaits/resources

TOOLCHAIN_PATH ?= /usr/local/arm-4.8.3/

include stmlib/makefile.inc

# The code is written in C++98, the default of the gcc 4.8 toolchain used for
# the original firmware. Required with more recent compilers.
CPPFLAGS += -std=gnu++98

# Code size.
#
# Loops with a variable number of iterations (most of them iterate over the
# samples of a block) are not unrolled, except in the voice and modal engine
# (the most expensive model of the original firmware) and in the duo mode
# string and speech models, where they are unrolled twice instead of 8 times,
# and in the stereo processing of duo mode, which is small and used by all
# the duo models.
# Loops with a small, constant number of iterations are still completely
# unrolled. This frees 11kB of flash for duo mode. The CPU use of the most
# expensive model of the original firmware is unchanged, and the other
# models remain below it.
CPPFLAGS += --param max-unroll-times=1
$(BUILD_DIR)voice.o \
$(BUILD_DIR)modal_engine.o \
$(BUILD_DIR)modal_voice.o \
$(BUILD_DIR)resonator.o \
$(BUILD_DIR)duo_string.o \
$(BUILD_DIR)duo_string_engine.o \
$(BUILD_DIR)duo_stereo.o: CPPFLAGS += --param max-unroll-times=2

# The stereo effects of duo mode, and the FM engine (whose loop also stores
# the carrier for a second pass), keep their constants in registers through
# their loops, instead of loading them again at every iteration.
$(BUILD_DIR)duo_stereo.o \
$(BUILD_DIR)fm_engine.o: CPPFLAGS += -fmove-loop-invariants

# Code which only runs at initialization or when settings are saved is
# optimized for size.
$(BUILD_DIR)settings.o \
$(BUILD_DIR)cv_adc.o \
$(BUILD_DIR)pots_adc.o \
$(BUILD_DIR)leds.o \
$(BUILD_DIR)switches.o \
$(BUILD_DIR)debug_port.o \
$(BUILD_DIR)firmware_update_adc.o \
$(BUILD_DIR)system_stm32f37x.o \
$(BUILD_DIR)stm32f37x_%.o: CCFLAGS += -Os

# Rule for building the firmware update file (needs numpy).
PYTHON ?= python3
wav:  $(TARGET_BIN)
	$(PYTHON) tools/qpsk_encoder.py \
		-t stm32f3 -k -s 48000 -b 12000 -c 6000 -p 256 \
		$(TARGET_BIN)
