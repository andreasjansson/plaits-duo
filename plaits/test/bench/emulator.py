#!/usr/bin/env python3
#
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
# Runs a bare-metal benchmark (see bench.mk) on an emulated Cortex-M4 (unicorn),
# and counts the cycles it would take on the STM32F373 of the module: 72MHz,
# code and constant data in flash, with 2 wait states and a 64-bit prefetch
# buffer.
#
# The model is simple: each instruction has the cycle count given by the
# Cortex-M4 reference manual (loads, stores, multiply-accumulates, divisions),
# a basic block takes the longer of its execution time and the time needed to
# fetch its code from flash (3 cycles per 8 bytes), a taken branch adds a
# pipeline refill, and each read of constant data from flash adds the wait
# states. The absolute counts are approximate, but the comparisons between
# two pieces of code built with the same compiler are meaningful.
#
# The benchmark calls bench_mark(tag) to delimit the measured blocks: the
# cycles between two calls are attributed to the tag of the second one (tags
# with bit 16 set only start a measurement).
#
# Usage: emulator.py BENCHMARK.elf RESULTS.json [--profile TAG]
#
# RESULTS.json receives the cycle counts of the blocks for each tag, the
# deepest stack use, and the hash of the output if the benchmark has one
# (bench_output_hash). --profile prints the functions in which the slowest
# block of the given tag (without its lower 8 bits) spends its time.

import bisect
import collections
import json
import subprocess
import sys

from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn import UC_HOOK_BLOCK, UC_HOOK_CODE, UC_PROT_ALL
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP
from unicorn.arm_const import UC_CPU_ARM_CORTEX_M4

FLASH_BASE, FLASH_SIZE = 0x08000000, 0x100000
# Constant data (see bench.ld), in a region whose reads are counted.
CONSTANTS_BASE, CONSTANTS_SIZE = 0x08100000, 0x100000
RAM_BASE, RAM_SIZE = 0x20000000, 0x10000
RAM_TOP = 0x20008000
WAIT_STATES = 2
TAKEN_BRANCH = 1 + WAIT_STATES

disassembler = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)


def instruction_cycles(mnemonic, operands):
  m = mnemonic.split('.')[0]
  if m in ('vdiv', 'vsqrt'):
    return 14
  if m in ('vmla', 'vmls', 'vnmla', 'vnmls', 'vfma', 'vfms', 'vfnma', 'vfnms'):
    return 3
  if m in ('vldr', 'vstr'):
    return 2 if m == 'vldr' else 1
  if m in ('vldmia', 'vldmdb', 'vstmia', 'vstmdb', 'vpush', 'vpop', 'vldm',
           'vstm'):
    return 1 + max(1, operands.count('s') - (1 if '!' in operands else 0))
  if m.startswith('ldrd') or m.startswith('strd'):
    return 3 if m.startswith('ldrd') else 2
  if m.startswith('ldm') or m == 'pop':
    return 2 + operands.count(',')
  if m.startswith('stm') or m == 'push':
    return 2 + operands.count(',')
  if m.startswith('ldr'):
    return 2
  if m.startswith('str'):
    return 1
  if m in ('sdiv', 'udiv'):
    return 7
  if m in ('tbb', 'tbh'):
    return 3
  return 1


class Block(object):
  """Cycle count of a basic block (execution and code fetch)."""

  def __init__(self, uc, address, size):
    self.end = address + size
    self.execution = 0
    self.literals = 0
    previous_is_memory = False
    code = bytes(uc.mem_read(address, size))
    for instruction in disassembler.disasm(code, address):
      m = instruction.mnemonic
      cycles = instruction_cycles(m, instruction.op_str)
      is_memory = m.startswith(('ldr', 'str', 'vldr', 'vstr'))
      if is_memory and previous_is_memory and cycles >= 2:
        cycles -= 1  # Neighbouring loads and stores are pipelined.
      previous_is_memory = is_memory
      if is_memory and '[pc' in instruction.op_str:
        cycles += WAIT_STATES  # Constant in flash.
        self.literals += 1
      self.execution += cycles
    self.fetch = ((size + 7) // 8) * (1 + WAIT_STATES)


def Run(path, profile_tag=None):
  uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
  uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)  # With its FPU.
  uc.mem_map(FLASH_BASE, FLASH_SIZE, UC_PROT_ALL)
  uc.mem_map(RAM_BASE, RAM_SIZE, UC_PROT_ALL)
  uc.mem_map(0xE0000000, 0x100000, UC_PROT_ALL)  # System control space.
  constants = bytearray(CONSTANTS_SIZE)
  with open(path, 'rb') as f:
    elf = ELFFile(f)
    for segment in elf.iter_segments():
      if segment['p_type'] == 'PT_LOAD' and segment['p_filesz']:
        a = segment['p_paddr']
        if CONSTANTS_BASE <= a < CONSTANTS_BASE + CONSTANTS_SIZE:
          offset = a - CONSTANTS_BASE
          constants[offset:offset + segment['p_filesz']] = segment.data()
        else:
          uc.mem_write(a, segment.data())
    symbols = elf.get_section_by_name('.symtab')
    addresses = dict((s.name, s['st_value']) for s in symbols.iter_symbols())
    functions = sorted(
        (s['st_value'] & ~1, s['st_size'], s.name)
        for s in symbols.iter_symbols()
        if s['st_info']['type'] == 'STT_FUNC' and s['st_size'] > 0)
    objects = sorted(
        (s['st_value'], s['st_size'], s.name)
        for s in symbols.iter_symbols()
        if s['st_info']['type'] == 'STT_OBJECT' and s['st_size'] > 0)
  function_starts = [f[0] for f in functions]
  object_starts = [o[0] for o in objects]

  def ObjectAt(address):
    i = bisect.bisect_right(object_starts, address) - 1
    if i >= 0 and address < objects[i][0] + objects[i][1]:
      return objects[i][2]
    return '?'


  def FunctionAt(address):
    i = bisect.bisect_right(function_starts, address) - 1
    if i >= 0 and address < functions[i][0] + functions[i][1]:
      return functions[i][2]
    return '?'

  state = {
      'cycles': 0, 'previous_end': None, 'last_mark': None,
      'lowest_sp': RAM_TOP}
  blocks = {}
  results = collections.defaultdict(list)
  current_profile = collections.Counter()
  current_reads = collections.Counter()
  current_literals = collections.Counter()
  slowest = [0, None, None, None]

  def OnBlock(uc, address, size, user_data):
    b = blocks.get(address)
    if b is None or b.end != address + size:
      b = blocks[address] = Block(uc, address, size)
    taken = state['previous_end'] not in (None, address)
    cycles = max(b.execution, b.fetch) + (TAKEN_BRANCH if taken else 0)
    state['cycles'] += cycles
    state['previous_end'] = b.end
    sp = uc.reg_read(UC_ARM_REG_SP)
    if RAM_BASE <= sp < state['lowest_sp']:
      state['lowest_sp'] = sp
    if profile_tag is not None:
      current_profile[FunctionAt(address)] += cycles
      if b.literals:
        current_literals[FunctionAt(address)] += b.literals

  def OnMark(uc, address, size, user_data):
    tag = uc.reg_read(UC_ARM_REG_R0)
    now = state['cycles']
    if state['last_mark'] is not None and not tag & 0x10000:
      cycles = now - state['last_mark']
      results[tag].append(cycles)
      if profile_tag is not None and tag >> 8 == profile_tag and \
          cycles > slowest[0]:
        slowest[0] = cycles
        slowest[1] = dict(current_profile)
        slowest[2] = dict(current_reads)
        slowest[3] = dict(current_literals)
    current_profile.clear()
    current_reads.clear()
    current_literals.clear()
    state['last_mark'] = now

  def ReadConstant(uc, offset, size, user_data):
    state['cycles'] += WAIT_STATES
    if profile_tag is not None:
      current_reads[ObjectAt(CONSTANTS_BASE + offset)] += 1
    return int.from_bytes(constants[offset:offset + size], 'little')

  def WriteConstant(uc, offset, size, value, user_data):
    raise RuntimeError('write to constant data at %x' % (
        CONSTANTS_BASE + offset))

  uc.mmio_map(CONSTANTS_BASE, CONSTANTS_SIZE, ReadConstant, None,
              WriteConstant, None)
  uc.hook_add(UC_HOOK_BLOCK, OnBlock)
  mark = addresses['bench_mark'] & ~1
  uc.hook_add(UC_HOOK_CODE, OnMark, begin=mark, end=mark)
  try:
    uc.emu_start(addresses['bench_reset'] | 1, addresses['bench_done'] & ~1)
  except UcError as e:
    sys.exit('emulation error at pc=%x: %s' % (uc.reg_read(UC_ARM_REG_PC), e))

  if profile_tag is not None and slowest[1]:
    print('slowest block of tag %x: %d cycles' % (profile_tag, slowest[0]))
    names = sorted(slowest[1].items(), key=lambda x: -x[1])[:15]
    demangled = subprocess.run(
        ['c++filt'], input='\n'.join(n for n, _ in names),
        capture_output=True, text=True).stdout.split('\n')
    for (name, cycles), d in zip(names, demangled):
      print('%7d %s' % (cycles, d[:110]))
    reads = sorted(slowest[2].items(), key=lambda x: -x[1])[:10]
    print('reads of constant data (flash): %d' % sum(slowest[2].values()))
    for name, n in reads:
      print('%7d %s' % (n, name))
    literals = sorted(slowest[3].items(), key=lambda x: -x[1])[:8]
    print('literal pool loads (flash): %d' % sum(slowest[3].values()))
    for name, n in literals:
      print('%7d %s' % (n, name))
  output = {
      'blocks': dict((str(tag), v) for tag, v in results.items()),
      'stack': RAM_TOP - state['lowest_sp']}
  # The hash of the benchmark's output, if it computes one (bench_duo.cc).
  if 'bench_output_hash' in addresses:
    address = addresses['bench_output_hash']
    data = bytes(uc.mem_read(address, 2 * 16 * 4))
    output['output_hash'] = [
        int.from_bytes(data[i:i + 4], 'little') for i in range(0, len(data), 4)]
  return output


if __name__ == '__main__':
  if len(sys.argv) not in (3, 5) or (len(sys.argv) == 5 and
                                     sys.argv[3] != '--profile'):
    sys.exit('Usage: %s BENCHMARK.elf RESULTS.json [--profile TAG]' %
             sys.argv[0])
  tag = int(sys.argv[4], 0) if len(sys.argv) == 5 else None
  results = Run(sys.argv[1], tag)
  with open(sys.argv[2], 'w') as f:
    json.dump(results, f)
