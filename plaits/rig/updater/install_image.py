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
# The image which installs the rig firmware and its updater with the
# bootloader: the firmware (at 0x08008000), blank flash up to the updater
# (0x0803c000), then the updater.
#
# Usage: install_image.py FIRMWARE.bin UPDATER.bin OUTPUT.bin

import sys

START = 0x08008000
UPDATER = 0x0803c000
END = 0x08040000


def main():
  if len(sys.argv) != 4:
    sys.exit(__doc__)
  firmware = open(sys.argv[1], 'rb').read()
  updater = open(sys.argv[2], 'rb').read()
  if len(firmware) > UPDATER - START:
    sys.exit('the firmware (%d bytes) overlaps the updater' % len(firmware))
  if len(updater) > END - UPDATER:
    sys.exit('the updater (%d bytes) does not fit' % len(updater))
  image = firmware + b'\xff' * (UPDATER - START - len(firmware)) + updater
  open(sys.argv[3], 'wb').write(image)


if __name__ == '__main__':
  main()
