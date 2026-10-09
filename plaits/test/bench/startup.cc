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
// Start-up code of the benchmarks (see bench.mk): enables the FPU, initializes
// the RAM and the static objects, runs main(), and stops the emulator.

#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;
typedef void (*Constructor)();
extern Constructor __init_array_start[], __init_array_end[];
int main();

// Delimits the measured blocks (see emulator.py).
extern "C" void __attribute__((noinline)) bench_mark(int tag) {
  asm volatile("nop" : : "r"(tag) : "memory");
}

extern "C" void __attribute__((noinline)) bench_done() {
  asm volatile("bkpt #0");
}

extern "C" void __cxa_pure_virtual() {
  while (1);
}

extern "C" void bench_start() {
  // Full access to the coprocessors 10 and 11 (the FPU).
  *(volatile uint32_t*)0xE000ED88 |= (0xF << 20);
  asm volatile("dsb\n isb\n");
  for (uint32_t* s = &_sidata, *d = &_sdata; d < &_edata; ) {
    *d++ = *s++;
  }
  for (uint32_t* d = &_sbss; d < &_ebss; ) {
    *d++ = 0;
  }
  for (Constructor* f = __init_array_start; f < __init_array_end; ++f) {
    (*f)();
  }
  main();
  bench_done();
  while (1);
}

extern "C" void __attribute__((section(".bench_entry"), naked)) bench_reset() {
  asm volatile("ldr sp, =0x20008000\n bl bench_start\n");
}

extern "C" {
void* __dso_handle = 0;
int __aeabi_atexit(void*, void (*)(void*), void*) { return 0; }
}
