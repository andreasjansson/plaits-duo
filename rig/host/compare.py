"""The tour recorded from the module against the same tour rendered by the virtual module
(plaits-sim-rig, the same knobs, CVs and commands), clip by clip.

    python compare.py rig/results/tour-... rig/results/virtual-tour-...

For each clip (aligned on its first note, the virtual one resampled to the recording's rate):
- the level of each note on OUT and AUX, against the render: the median difference (the
  analog path's gain, the same for every clip) and the spread around it;
- dropouts: 2ms windows where the module is more than 10dB below the render (offset by that
  median) while the render is loud;
- clicks: 2ms windows where the module has more than 12dB more energy above 6kHz than the
  render;
- the spectrum: the largest difference in a third-octave band (relative to each one's total,
  down to -60dB).

The renders differ by the drift of the stereo effects and the phases of the oscillators,
which these measures ignore, and by the module's input stages (a few cents of pitch).
"""
import json
import re
import sys
from fractions import Fraction
from pathlib import Path

import numpy as np
from scipy.signal import butter, resample_poly, sosfilt

import tour

WINDOW = 0.002


def clips(folder):
    out = {}
    for path in sorted(Path(folder).glob("*-tour-*-[0-9].npz")):
        layout, model = re.match(r"\d+-tour-(\w+)-(\d)\.npz", path.name).groups()
        d = np.load(path)
        out[(layout, int(model) - 1)] = (d["y"].astype(np.float64), float(d["sr"]))
    return out


def aligned(y, sr, sr_out):
    start = tour.first_onset(y, sr)
    y = y[max(0, int((start - 0.05) * sr)):]
    if sr != sr_out:
        q = Fraction(int(sr_out), int(round(sr)))
        y = resample_poly(y, q.numerator, q.denominator, axis=0)
    return y


def envelope(x, sr, window=WINDOW):
    n = int(window * sr)
    m = len(x) // n
    return np.sqrt(np.mean(x[:m * n].reshape(m, n, -1) ** 2, axis=1)) + 1e-7


def events(mask, window=WINDOW, merge=0.01):
    """(start s, length s) of the runs of True, merged when closer than merge."""
    idx = np.flatnonzero(mask)
    if not idx.size:
        return []
    runs = np.split(idx, np.flatnonzero(np.diff(idx) > merge / window) + 1)
    return [(round(r[0] * window, 3), round((r[-1] - r[0] + 1) * window, 3)) for r in runs]


def bands(x, sr, floor=-60):
    """Third-octave bands, in dB of the total, at least floor (the recording's noise)."""
    spec = np.abs(np.fft.rfft(x, axis=0)) ** 2
    f = np.fft.rfftfreq(len(x), 1 / sr)
    edges = 50 * 2 ** (np.arange(0, 29) / 3)
    total = spec.sum(0) + 1e-12
    b = np.array([10 * np.log10(spec[(f >= lo) & (f < hi)].sum(0) / total + 1e-12)
                  for lo, hi in zip(edges[:-1], edges[1:])])
    return np.maximum(b, floor)


def compare_clip(ym, ys, sr):
    n = min(len(ym), len(ys), int((0.05 + tour.LOOPS * tour.LOOP) * sr))
    ym, ys = ym[:n], ys[:n]
    # notes
    note = int(tour.NOTE * sr)
    first = int(0.05 * sr)
    level = []
    for i in range(tour.LOOPS * len(tour.MELODY)):
        a = first + i * note
        m = np.sqrt(np.mean(ym[a:a + note] ** 2, 0)) + 1e-7
        s = np.sqrt(np.mean(ys[a:a + note] ** 2, 0)) + 1e-7
        level.append(20 * np.log10(m / s))
    level = np.array(level)
    offset = np.median(level, 0)
    spread = np.std(level - offset, 0)
    # dropouts and clicks
    em, es = envelope(ym, sr), envelope(ys, sr)
    ratio = 20 * np.log10(em / es) - offset
    loud = 20 * np.log10(es) > -40
    drops = events(np.any((ratio < -10) & loud, 1))
    sos = butter(4, 6000, "highpass", fs=sr, output="sos")
    hm, hs = envelope(sosfilt(sos, ym, axis=0), sr), envelope(sosfilt(sos, ys, axis=0), sr)
    hratio = 20 * np.log10(hm / hs) - offset
    clicks = events(np.any((hratio > 12) & (20 * np.log10(hm) > -60), 1))
    spectrum = bands(ym, sr) - bands(ys, sr)
    return {"offset_db": offset.tolist(), "note_spread_db": spread.tolist(),
            "worst_note_db": np.abs(level - offset).max(0).tolist(),
            "dropouts": drops, "clicks": clicks,
            "spectrum_db": float(np.max(np.abs(spectrum)))}


def compare(module_folder, virtual_folder):
    module, virtual = clips(module_folder), clips(virtual_folder)
    results = {}
    for key in sorted(module, key=lambda k: (k[0] != "split", k[1])):
        if key not in virtual:
            continue
        ym, srm = module[key]
        ys, srs = virtual[key]
        r = compare_clip(aligned(ym, srm, srm), aligned(ys, srs, srm), srm)
        results[f"{key[0]}-{key[1] + 1}"] = r
        print(f"{key[0]:6s} {key[1] + 1} {tour.MODELS[key[1]]:18s} "
              f"level {r['offset_db'][0]:+5.1f}/{r['offset_db'][1]:+5.1f}dB "
              f"spread {r['note_spread_db'][0]:.1f}/{r['note_spread_db'][1]:.1f} "
              f"worst {max(r['worst_note_db']):4.1f}  spectrum {r['spectrum_db']:4.1f}dB  "
              f"dropouts {len(r['dropouts']):3d}  clicks {len(r['clicks']):3d}")
    return results


if __name__ == "__main__":
    out = compare(sys.argv[1], sys.argv[2])
    path = Path(sys.argv[1]) / "compare.json"
    path.write_text(json.dumps(out, indent=1))
    print(path)
