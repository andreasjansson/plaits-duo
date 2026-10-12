"""Decodes the telemetry of the rig firmware (plaits/rig/telemetry.h) from a recording of AUX.

    frames = decode(aux, sr)        # aux: float array, sr: its sample rate
    frames[0].cv_mean(1)            # LEVEL reading (-1 to 1), averaged over the frame's 2s

About every 2s (8000 blocks of 12 samples at the module's 47872Hz, or as set by a command),
AUX carries a frame instead of the voice: a preamble of 32 zero bits, then the words of
TelemetryFrame (version 1: 34 words, 2: 65, 3: 81, 4: 89), most significant bit first, as a Manchester code
of 6 module samples per bit (a 1: high then low). The updater (plaits/rig/updater) sends its
state on AUX in the same code (UpdaterStatus, 8 words): decode() returns those frames too
(kind "updater").

The decoder
1. finds the preambles by correlation with their square wave (either polarity: the module's
   output stage inverts, and so may the recording chain),
2. refines each frame's start and bit length (the two clocks differ by tens of ppm) to
   maximise the margins of the preamble and the first words,
3. reads each bit as the sign of (first half - second half), and checks the magic word and
   the checksum (FNV-1a). Each frame keeps its weakest bit's margin, relative to the median.
"""
import struct
from dataclasses import dataclass, field

import numpy as np
from scipy.signal import fftconvolve

MODULE_RATE = 47872.0
SAMPLES_PER_BIT = 6
PREAMBLE_BITS = 32
MAGIC = 0x504C5247
MAGIC_UPDATER = 0x504C5550
BLOCK_SIZE = 12
FRAME_INTERVAL_BLOCKS = 8000
BLOCK_CYCLES = 18048            # processor cycles in a block
V1_1_MOST_CYCLES = 15268        # the most expensive block of the original firmware v1.1

# TelemetryFrame, field by field (little-endian, as in the processor's memory).
FIELDS = [
    ("magic", "I"), ("version_size", "I"), ("frame", "I"),
    ("blocks", "I"), ("cycles", "I"), ("render_cycles", "I"), ("poll_cycles", "I"),
    ("overruns", "I"),
    ("cv_sum", "2i"), ("cv_sum_squares", "4I"),
    ("cv", "8h"), ("pots", "8H"),
    ("level_note_scale", "f"),
    ("v_oct_calibration", "2f"), ("level_calibration", "2f"), ("level_note_calibration", "2f"),
    ("engine", "B"), ("duo_mode", "B"), ("duo_engine", "B"), ("active_engine", "B"),
    ("ui_mode", "B"), ("patched", "B"), ("calibrating", "B"), ("padding", "B"),
    ("note", "f"), ("level_note", "f"),
    ("checksum", "I"),
]
FIELDS_V1 = FIELDS
FIELDS_V2 = FIELDS[:-1] + [
    ("image", "I"), ("commands", "I"),
    ("command_errors", "H"), ("last_command", "B"), ("instrument", "B"),
    ("section_cycles", "4I"), ("engine_cycles", "16I"), ("engine_overruns", "16H"),
    ("checksum", "I"),
]
FIELDS_V3 = FIELDS_V2[:-1] + [
    ("probe_max", "8I"), ("probe_sum", "8I"),
    ("checksum", "I"),
]
FIELDS_V4 = FIELDS_V3[:-1] + [
    ("probe_count", "8I"),
    ("checksum", "I"),
]
FIELDS_UPDATER = [
    ("magic", "I"), ("version_size", "I"), ("state", "I"), ("packets", "I"), ("pages", "I"),
    ("errors", "I"), ("peak", "I"), ("checksum", "I"),
]
FIELDS_UPDATER_V2 = FIELDS_UPDATER[:-1] + [
    ("last_error", "I"), ("demodulator_state", "I"), ("checksum", "I"),
]
# (magic, version): fields.
LAYOUTS = {(MAGIC, 1): FIELDS_V1, (MAGIC, 2): FIELDS_V2, (MAGIC, 3): FIELDS_V3, (MAGIC, 4): FIELDS_V4,
           (MAGIC_UPDATER, 1): FIELDS_UPDATER,
           (MAGIC_UPDATER, 2): FIELDS_UPDATER_V2}
UPDATER_ERRORS = ["none", "overflow", "crc", "sync", "too large", "no data"]


def _words(fields):
    return struct.calcsize("<" + "".join(f for _, f in fields)) // 4


WORDS = _words(FIELDS_V4)
BITS = PREAMBLE_BITS + 32 * WORDS                       # the longest frame
MIN_BITS = PREAMBLE_BITS + 32 * _words(FIELDS_UPDATER)  # the shortest
SECTIONS = ["control", "engine", "stereo", "output"]
UPDATER_STATES = ["waiting", "receiving", "error", "writing", "done"]

CV_CHANNELS = ["model", "v_oct", "fm", "harmonics", "timbre", "morph", "trigger", "level"]
POTS = ["frequency", "harmonics", "timbre", "morph", "timbre_cv", "fm_cv", "morph_cv"]
PATCHED = ["fm", "timbre", "morph", "trigger", "level"]
UI_MODES = ["normal", "alternate", "octave", "calibration_c1", "calibration_c3", "error"]


@dataclass
class Frame:
    t: float                   # seconds: start of the preamble in the recording
    end: float                 # seconds: end of the frame's last bit
    margin: float              # weakest bit / median bit
    inverted: bool
    data: dict = field(default_factory=dict)

    def __getattr__(self, name):
        try:
            return self.__dict__["data"][name]
        except KeyError:
            raise AttributeError(name)

    @property
    def kind(self):
        return "updater" if self.data["magic"] == MAGIC_UPDATER else "telemetry"

    @property
    def version(self):
        return self.data["version_size"] >> 16

    def cv_mean(self, i):
        """Mean reading (-1 to 1) of V/OCT (0) or LEVEL (1) since the previous frame."""
        return self.cv_sum[i] / max(1, self.blocks) / 32768.0

    def cv_std(self, i):
        """Standard deviation of the same readings (-1 to 1)."""
        n = max(1, self.blocks)
        squares = self.cv_sum_squares[2 * i] + (self.cv_sum_squares[2 * i + 1] << 32)
        variance = (squares * n - self.cv_sum[i] ** 2) / (n * n)   # exact, in integers
        return float(np.sqrt(max(0, variance))) / 32768.0

    @property
    def layout(self):
        return "split" if self.duo_engine & 8 else "stereo"

    def is_patched(self, jack):
        return bool(self.patched >> PATCHED.index(jack) & 1)

    def summary(self):
        d = self.data
        if self.kind == "updater":
            state = UPDATER_STATES[d["state"]] if d["state"] < len(UPDATER_STATES) else d["state"]
            last = ""
            if "last_error" in d and d["errors"]:
                e = d["last_error"]
                last = (f" (last: {UPDATER_ERRORS[e] if e < len(UPDATER_ERRORS) else e}, "
                        f"demodulator {d['demodulator_state']})")
            return (f"updater: {state}, {d['packets']} packets, {d['pages']} pages written, "
                    f"{d['errors']} errors{last}, peak {d['peak']}")
        extra = ""
        if self.version >= 2:
            extra = (f", image {d['image']:08x}, commands {d['commands']} "
                     f"(errors {d['command_errors']}, last {d['last_command']}), "
                     f"instrument {d['instrument']}, sections "
                     + "/".join(str(c) for c in d["section_cycles"]))
        return (f"frame {d['frame']}: {'duo ' + self.layout if d['duo_mode'] else 'original'} "
                f"model {d['duo_engine'] & 7 if d['duo_mode'] else d['engine']} "
                f"(active {d['active_engine']}), ui {UI_MODES[d['ui_mode']] if d['ui_mode'] < len(UI_MODES) else d['ui_mode']}, "
                f"patched {','.join(j for j in PATCHED if self.is_patched(j)) or '-'}, "
                f"V/OCT {self.cv_mean(0):+.5f} LEVEL {self.cv_mean(1):+.5f}, "
                f"cycles {d['cycles']} (render {d['render_cycles']}, poll {d['poll_cycles']}), "
                f"overruns {d['overruns']}" + extra)


def _unpack(words, fields):
    raw = struct.pack("<%dI" % len(words), *words)
    values = struct.unpack("<" + "".join(f for _, f in fields), raw)
    data, i = {}, 0
    for name, f in fields:
        n = int(f[:-1]) if len(f) > 1 else 1
        data[name] = list(values[i:i + n]) if n > 1 else values[i]
        i += n
    return data


def _checksum(words):
    h = 2166136261
    for w in words:
        h = ((h ^ w) * 16777619) & 0xFFFFFFFF
    return h


def _cumsum(x):
    return np.concatenate([[0.0], np.cumsum(x)])


def _integral(c, a, b):
    """Integral of the signal (whose cumulative sum is c) from fractional sample a to b."""
    def at(p):
        p = np.clip(p, 0, len(c) - 2)
        i = np.floor(p).astype(int)
        return c[i] + (p - i) * (c[i + 1] - c[i])
    return at(b) - at(a)


def _bit_values(c, start, period, n):
    """first half - second half, for n bits from `start` (fractional samples)."""
    k = np.arange(n)
    s = start + k * period
    return _integral(c, s, s + period / 2) - _integral(c, s + period / 2, s + period)


def _candidates(x, period):
    """Sample indices where a preamble may start, and the sign of its correlation: in each
    stretch where the normalized correlation with the preamble's square wave is high, where
    the correlation itself peaks (the window being then all preamble). Loud audio can score
    high too, so every stretch is a candidate (decode keeps those that decode)."""
    n = int(round(PREAMBLE_BITS * period))
    k = np.arange(n)
    template = np.where((k % period) < period / 2, -1.0, 1.0)   # zero bits: low, then high
    corr = fftconvolve(x, template[::-1], mode="valid")
    energy = np.sqrt(np.maximum(0, fftconvolve(x * x, np.ones(n), mode="valid") * n)) + 1e-12
    idx = np.flatnonzero(np.abs(corr / energy) > 0.7)
    if idx.size == 0:
        return []
    out = []
    for run in np.split(idx, np.flatnonzero(np.diff(idx) > n // 4) + 1):
        lo, hi = run[0], min(len(corr), run[-1] + int(2 * period) + 1)
        best = lo + int(np.argmax(np.abs(corr[lo:hi])))
        out.append((best, np.sign(corr[best])))
    return out


def decode(x, sr, return_rate=False, kind="telemetry"):
    """[Frame] found in x (float array at sample rate sr): of the telemetry, of the updater,
    or (kind None) both."""
    x = np.asarray(x, dtype=np.float64)
    x = x - np.mean(x)
    period0 = SAMPLES_PER_BIT * sr / MODULE_RATE
    c = _cumsum(x)
    frames = []
    for start0, _ in _candidates(x, period0):
        if start0 + (MIN_BITS + 1) * period0 > len(x):
            continue
        if frames and start0 < frames[-1].end * sr:
            continue   # inside the frame just decoded
        # Refine the start (within half a bit: the all-zero preamble, shifted by half a bit,
        # looks inverted) and the bit length (+-300 ppm). At the right alignment, every bit
        # has a transition in its middle: the mean |first half - second half| is the largest
        # (half a bit off, it is 0 wherever two consecutive bits differ).
        best = None
        for dp in np.linspace(-3e-4, 3e-4, 7):
            period = period0 * (1 + dp)
            for ds in np.arange(-period / 2 - 1, period / 2 + 1.01, 0.25):
                score = np.mean(np.abs(_bit_values(c, start0 + ds, period, MIN_BITS)))
                if best is None or score > best[0]:
                    best = (score, start0 + ds, period)
        _, start, period = best
        # The preamble's zeros give the phase of the bits, not where the words start (nor,
        # without them, the polarity): the magic word does.
        found = None
        for shift in range(-4, 5):
            s = start + shift * period
            if s < 0 or s + MIN_BITS * period > len(x):
                continue
            n_bits = min(BITS, int((len(x) - s) / period))
            v = _bit_values(c, s, period, n_bits)
            for sign in (1.0, -1.0):
                bits = (sign * v[PREAMBLE_BITS:] > 0).astype(np.int64)
                head = [int("".join(map(str, bits[i * 32:(i + 1) * 32])), 2) for i in (0, 1)]
                fields = LAYOUTS.get((head[0], head[1] >> 16))
                if fields is None or head[1] & 0xFFFF != _words(fields):
                    continue
                n = _words(fields)
                if PREAMBLE_BITS + 32 * n > n_bits:
                    continue
                words = [int("".join(map(str, bits[i * 32:(i + 1) * 32])), 2)
                         for i in range(n)]
                if _checksum(words[:-1]) == words[-1]:
                    found = (s, sign * v[:PREAMBLE_BITS + 32 * n], words, sign < 0, fields)
                    break
            if found:
                break
        if not found:
            continue
        start, v, words, inverted, fields = found
        strength = np.abs(v)
        frames.append(Frame(t=start / sr, end=(start + len(v) * period) / sr,
                            margin=float(strength.min() / (np.median(strength) + 1e-12)),
                            inverted=inverted, data=_unpack(words, fields)))
    frames_t = [f for f in frames if f.kind == "telemetry"]
    if kind is not None:
        frames = [f for f in frames if f.kind == kind]
    if return_rate:
        return frames, module_rate(frames_t, sr)
    return frames


def module_rate(frames, sr):
    """The module's sample rate, from the spacing of consecutive frames (None if < 2)."""
    pairs = [(a, b) for a, b in zip(frames, frames[1:]) if b.frame == a.frame + 1]
    if not pairs:
        return None
    dt = np.mean([b.t - a.t for a, b in pairs])
    return FRAME_INTERVAL_BLOCKS * BLOCK_SIZE / dt


def mask(n, sr, frames, guard=0.005):
    """Boolean array of n samples: True outside the telemetry frames (and a guard around them).
    Frames come every 8000 blocks: from the decoded ones, also those cut off at the ends of
    the recording, which cannot be decoded."""
    keep = np.ones(n, bool)
    period = FRAME_INTERVAL_BLOCKS * BLOCK_SIZE / MODULE_RATE
    for f in frames:
        length = f.end - f.t
        for k in range(-int(n / sr / period) - 1, int(n / sr / period) + 2):
            t = f.t + k * period
            a = max(0, int((t - guard) * sr))
            b = min(n, int((t + length + guard) * sr) + 1)
            if a < b:
                keep[a:b] = False
    return keep


def bursts(x, sr, threshold=0.5):
    """Spans (start, end) in seconds of anything that looks like a telemetry burst, decoded or
    not: to mask AUX even when a frame is damaged."""
    period = SAMPLES_PER_BIT * sr / MODULE_RATE
    spans = []
    for start, _ in _candidates(np.asarray(x, float) - np.mean(x), period):
        spans.append((start / sr, (start + BITS * period) / sr))
    return spans
