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
// A table of the flash memory, copied to RAM (to the memory the engines
// share, see DuoVoice::Init) a little at every block, so that no block takes
// much longer: on the module, each read of the flash memory takes about 5
// cycles more than a read of the RAM (its wait states, and the prefetch of
// the code it interrupts), which the emulated benchmark does not count.

#ifndef PLAITS_DSP_DUO_DUO_RAM_TABLE_H_
#define PLAITS_DSP_DUO_DUO_RAM_TABLE_H_

#include "stmlib/stmlib.h"
#include "stmlib/utils/buffer_allocator.h"

namespace plaits {

// Bytes copied at each block (about 300 cycles on the module).
const size_t kDuoRamTableStep = 256;

template<typename T>
class DuoRamTable {
 public:
  DuoRamTable() { }
  ~DuoRamTable() { }

  void Init(stmlib::BufferAllocator* allocator, const T* flash, size_t size) {
    flash_ = flash;
    size_ = size;
    ram_ = allocator->Allocate<T>(size);
    copied_ = 0;
  }

  // The RAM was used by another engine.
  void Reset() {
    copied_ = 0;
  }

  // Copies a little more, and returns the table to read.
  inline const T* Update() {
    if (!ram_ || copied_ == size_) {
      return ram_ ? ram_ : flash_;
    }
    size_t n = kDuoRamTableStep / sizeof(T);
    if (n > size_ - copied_) {
      n = size_ - copied_;
    }
    const T* from = flash_ + copied_;
    T* to = ram_ + copied_;
    copied_ += n;
    while (n--) {
      *to++ = *from++;
    }
    return copied_ == size_ ? ram_ : flash_;
  }

 private:
  const T* flash_;
  T* ram_;
  size_t size_;
  size_t copied_;

  DISALLOW_COPY_AND_ASSIGN(DuoRamTable);
};

}  // namespace plaits

#endif  // PLAITS_DSP_DUO_DUO_RAM_TABLE_H_
