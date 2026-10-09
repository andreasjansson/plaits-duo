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
// Simulator version of stmlib/system/storage.h: the "flash" is kept in memory
// (so that it survives a simulated reboot), and in the file named by the
// PLAITS_SIM_FLASH environment variable, if it is set (except in the cycle
// count benchmarks, which run on a bare-metal ARM emulator).
//
// As with the real ChunkStorage, Init() returns false and writes the default
// values when no valid data is found.

#ifndef STMLIB_SYSTEM_STORAGE_H_
#define STMLIB_SYSTEM_STORAGE_H_

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "stmlib/stmlib.h"

namespace stmlib {

namespace sim_flash {

struct Chunk {
  uint32_t tag;
  uint32_t size;
  uint8_t data[256];
};

struct Image {
  bool valid;
  Chunk persistent;
  Chunk state;
};

extern Image image;

inline void Load() {
#ifndef BENCH
  const char* path = getenv("PLAITS_SIM_FLASH");
  if (!path) {
    return;
  }
  FILE* fp = fopen(path, "rb");
  if (!fp) {
    return;
  }
  Image loaded;
  if (fread(&loaded.persistent, sizeof(Chunk), 1, fp) == 1 &&
      fread(&loaded.state, sizeof(Chunk), 1, fp) == 1) {
    loaded.valid = true;
    image = loaded;
  }
  fclose(fp);
#endif  // BENCH
}

inline void Save() {
#ifndef BENCH
  const char* path = getenv("PLAITS_SIM_FLASH");
  if (!path) {
    return;
  }
  FILE* fp = fopen(path, "wb");
  if (!fp) {
    return;
  }
  fwrite(&image.persistent, sizeof(Chunk), 1, fp);
  fwrite(&image.state, sizeof(Chunk), 1, fp);
  fclose(fp);
#endif  // BENCH
}

}  // namespace sim_flash

template<
    uint32_t flash_start,
    uint32_t flash_end,
    typename PersistentData,
    typename StateData>
class ChunkStorage {
 public:
  ChunkStorage() { }
  ~ChunkStorage() { }

  bool Init(PersistentData* persistent_data, StateData* state_data) {
    persistent_data_ = persistent_data;
    state_data_ = state_data;
    sim_flash::Load();
    const sim_flash::Image& image = sim_flash::image;
    if (image.valid &&
        image.persistent.tag == PersistentData::tag &&
        image.persistent.size == sizeof(PersistentData) &&
        image.state.tag == StateData::tag &&
        image.state.size == sizeof(StateData)) {
      memcpy(persistent_data, image.persistent.data, sizeof(PersistentData));
      memcpy(state_data, image.state.data, sizeof(StateData));
      return true;
    }
    SavePersistentData();
    return false;
  }

  void SaveState() {
    Write(&sim_flash::image.state, state_data_);
    sim_flash::Save();
  }

  void SavePersistentData() {
    Write(&sim_flash::image.persistent, persistent_data_);
    Write(&sim_flash::image.state, state_data_);
    sim_flash::image.valid = true;
    sim_flash::Save();
  }

 private:
  template<typename T>
  void Write(sim_flash::Chunk* chunk, const T* data) {
    chunk->tag = T::tag;
    chunk->size = sizeof(T);
    memcpy(chunk->data, data, sizeof(T));
  }

  PersistentData* persistent_data_;
  StateData* state_data_;

  DISALLOW_COPY_AND_ASSIGN(ChunkStorage);
};

}  // namespace stmlib

#endif  // STMLIB_SYSTEM_STORAGE_H_
