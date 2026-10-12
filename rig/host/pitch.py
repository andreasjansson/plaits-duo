"""The frequency of a voice, from a recording of OUT or AUX.

    f = frequency(x, sr, keep)      # keep: boolean array, False where AUX carries telemetry
    cents(f_aux, f_out)             # the interval between the two voices

1. A coarse estimate of the period, from the normalized difference function (as in YIN) of
   the longest stretch of kept samples.
2. The fundamental is demodulated (x * exp(-2 pi i f t)) and averaged over 4 periods (which
   removes its harmonics); the slope of its phase gives the frequency, over each kept
   stretch (a gap would break the phase), weighted by their lengths.
For a steady tone, the result is good to well under 0.1 cent in a second.
"""
import numpy as np


def stretches(keep, min_len):
    """(start, end) of the runs of True in keep at least min_len long."""
    k = np.concatenate([[False], np.asarray(keep, bool), [False]])
    d = np.diff(k.astype(int))
    starts, ends = np.flatnonzero(d == 1), np.flatnonzero(d == -1)
    return [(a, b) for a, b in zip(starts, ends) if b - a >= min_len]


def coarse(x, sr, fmin=25.0, fmax=8000.0):
    """The period's frequency by the normalized difference function, or None."""
    x = np.asarray(x, float) - np.mean(x)
    n = min(len(x), int(0.25 * sr))
    if n < int(3 * sr / fmin):
        n = min(len(x), int(3 * sr / fmin))
    x = x[:n]
    max_lag = min(n // 2, int(sr / fmin))
    min_lag = max(2, int(sr / fmax))
    w = n - max_lag
    if w <= 0 or np.sum(x * x) < 1e-12:
        return None
    # d(tau) = sum (x[j] - x[j + tau])^2 over a window of w samples
    spec = np.fft.rfft(x, 2 * n)
    acf = np.fft.irfft(spec * np.conj(spec))[:max_lag + 1]
    c = np.concatenate([[0.0], np.cumsum(x * x)])
    taus = np.arange(max_lag + 1)
    e0 = c[w]
    et = c[taus + w] - c[taus]
    # the ACF over a window of w samples, approximated by the full ACF scaled
    d = e0 + et - 2 * acf * (w / n)
    dn = np.ones_like(d)
    dn[1:] = d[1:] * taus[1:] / np.maximum(np.cumsum(d[1:]), 1e-12)
    candidates = np.flatnonzero((dn[1:-1] < dn[:-2]) & (dn[1:-1] <= dn[2:]) & (dn[1:-1] < 0.3)) + 1
    candidates = candidates[candidates >= min_lag]
    if len(candidates) == 0:
        tau = int(np.argmin(dn[min_lag:]) + min_lag)
    else:
        tau = int(candidates[0])
    shift = 0.0
    if 1 <= tau < max_lag:
        a, b, c3 = dn[tau - 1], dn[tau], dn[tau + 1]
        denom = a - 2 * b + c3
        if denom > 0:
            shift = float(np.clip(0.5 * (a - c3) / denom, -1.0, 1.0))
    f = sr / (tau + shift)
    return f if fmin <= f <= fmax else None


def refine(x, sr, f, start=0):
    """(frequency, weight) from the phase of the fundamental over x (one kept stretch)."""
    x = np.asarray(x, float) - np.mean(x)
    n = len(x)
    t = (np.arange(n) + start) / sr
    z = x * np.exp(-2j * np.pi * f * t)
    k = max(1, int(round(4 * sr / f)))
    if n < 3 * k:
        return None
    zs = np.convolve(z, np.ones(k) / k, mode="valid")
    step = max(1, k // 8)
    zs = zs[::step]
    phase = np.unwrap(np.angle(zs))
    tt = (np.arange(len(zs)) * step + (k - 1) / 2) / sr
    slope = np.polyfit(tt, phase, 1)[0]
    return f + slope / (2 * np.pi), n


def frequency(x, sr, keep=None, fmin=25.0, fmax=8000.0, near=None):
    """The fundamental frequency (Hz) of x over the kept samples, or None. With `near`,
    the partial closest to that frequency instead (to measure two voices, or two notes, at
    the same partial when the waveform has strong harmonics)."""
    x = np.asarray(x, float)
    keep = np.ones(len(x), bool) if keep is None else np.asarray(keep, bool)
    runs = stretches(keep, int(0.08 * sr))
    if not runs:
        return None
    a, b = max(runs, key=lambda r: r[1] - r[0])
    f = near if near else coarse(x[a:b], sr, fmin, fmax)
    if f is None:
        return None
    # twice: the first pass gets within a fraction of a hertz
    for _ in range(2):
        parts = [refine(x[a:b], sr, f, a) for a, b in runs]
        parts = [p for p in parts if p is not None]
        if not parts:
            return None
        f = sum(fp * w for fp, w in parts) / sum(w for _, w in parts)
    # a tone the estimate could not follow (no clear period)
    if not (np.isfinite(f) and fmin * 0.9 <= f <= fmax * 1.1):
        return None
    return float(f)


def track(x, sr, f, keep=None, window=0.05):
    """The frequency in successive windows (for the jitter of a voice): [(t, f)]. A window
    holds at least 16 periods (the phase is averaged over 4)."""
    x = np.asarray(x, float)
    keep = np.ones(len(x), bool) if keep is None else np.asarray(keep, bool)
    n = max(int(window * sr), int(16 * sr / f))
    out = []
    for a in range(0, len(x) - n, n):
        if keep[a:a + n].all():
            r = refine(x[a:a + n], sr, f, a)
            if r:
                out.append(((a + n / 2) / sr, r[0]))
    return out


def cents(f, f_ref):
    return 1200.0 * np.log2(f / f_ref)
