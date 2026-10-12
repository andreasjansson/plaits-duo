"""The loops of the web demo (web/index.html), recorded from the module with spoken titles:
each loop as the page plays it in duo mode (its model, the stereo layout, the countermelody
on LEVEL), with the knobs where the page starts them.

The CVs come from the page's own sequencer (web_loops.js runs web/loops.js and the Loop
class of web/worklet.js), block by block, and are played by the oC: the notes as steps,
the modulations as glides between the points where a straight line stops fitting them.
The pitches are in volts as in the tour (FREQUENCY at 9 o'clock, 2V: about D3), which keeps
the second voice above the lowest voltage LEVEL reads.

    rig.py loops              writes loops.wav and loops.m4a in the results folder
"""
import json
import subprocess
from pathlib import Path

import numpy as np

import telemetry
import tour

HERE = Path(__file__).resolve().parent
SECONDS = 12.0                 # of each loop, at most (two cycles, when they are short)
MAX_STEPS = 1000               # the oC's sequences hold 1024 steps
BASE = 2.0                     # volts of the pitch set by FREQUENCY, as in the tour
TRIGGER_MS = 10
KNOBS = {"frequency": tour.KNOBS["frequency"], "harmonics": 0.5, "timbre": 0.5,
         "morph": 0.5, "timbre_cv": 0.85, "fm_cv": 0.5, "morph_cv": 0.85}
# The knobs the page sets when it selects a model (KNOB_PRESETS in web/index.html), which
# stay for the following loops.
PRESETS = {7: {"harmonics": 0.2}, 6: {"timbre": 0.65, "morph": 0.8}}
TOLERANCE = {"pitch": 0.08 / 12, "level": 0.08 / 12, "timbre": 0.04, "morph": 0.04,
             "harmonics": 0.04}


def page_loops(seconds=SECONDS):
    out = subprocess.run(["node", str(HERE / "web_loops.js"), str(seconds)],
                         capture_output=True, text=True, check=True).stdout
    return json.loads(out)


def segments(v, dt, tolerance, jump):
    """A CV sampled every dt seconds as oC steps [(t_ms, volts, glide_ms)]: straight lines
    (swing door: each as long as it stays within tolerance of the samples), and jumps where
    two samples are more than jump apart."""
    v = np.asarray(v, dtype=np.float64)
    steps = [(0.0, v[0], 0)]
    start = 0
    lo, hi = -np.inf, np.inf
    k = 1
    while k < len(v):
        if abs(v[k] - v[k - 1]) > jump:
            if k - 1 > start:
                steps.append(_line(start, k - 1, v, dt))
            steps.append((k * dt * 1000, v[k], 0))
            start, lo, hi = k, -np.inf, np.inf
            k += 1
            continue
        n = k - start
        slope_lo = (v[k] - tolerance - v[start]) / n
        slope_hi = (v[k] + tolerance - v[start]) / n
        if max(lo, slope_lo) > min(hi, slope_hi):
            steps.append(_line(start, k - 1, v, dt))
            start, lo, hi = k - 1, -np.inf, np.inf
            continue
        lo, hi = max(lo, slope_lo), min(hi, slope_hi)
        k += 1
    if len(v) - 1 > start:
        steps.append(_line(start, len(v) - 1, v, dt))
    # Flat lines need no step.
    out, current = [], None
    for t, volts, glide in steps:
        if current is None or abs(volts - current) > 1e-4:
            out.append((t, volts, glide))
            current = volts
    return out


def _line(a, b, v, dt):
    # From the sample a (where the output already is) to the sample b.
    return (a * dt * 1000 + 1, v[b], max(2, int(round((b - a) * dt * 1000))))


def oc_steps(loop, seconds):
    """The oC steps of a loop, for its first `seconds` (t_ms, role, volts[, glide_ms])."""
    cv, dt = loop["cv"], loop["dt"]
    n = int(seconds / dt)
    out = []
    for role, key, scale, offset, jump in (("pitch", "a", 1 / 12, BASE, 0.3 / 12),
                                           ("level", "b", 1 / 12, BASE, 0.3 / 12),
                                           ("timbre", "timbre", 1.0, 0.0, 0.5),
                                           ("morph", "morph", 1.0, 0.0, 0.5),
                                           ("harmonics", "harmonics", 1.0, 0.0, 0.5)):
        v = np.asarray(cv[key][:n]) * scale + offset
        for t, volts, glide in segments(v, dt, TOLERANCE[role], jump):
            out.append((t, role, volts, glide))
    trigger = np.asarray(cv["trigger"][:n])
    for k in np.flatnonzero(np.diff(trigger) > 0) + 1:
        t = k * dt * 1000
        out += [(t, "trigger", 5.0), (t + TRIGGER_MS, "trigger", 0.0)]
    return sorted(out, key=lambda step: step[0])


def fitting(loop, seconds=SECONDS):
    """Two cycles (or `seconds`, if shorter), shortened until the steps fit in the oC."""
    seconds = min(seconds, max(2 * loop["cycle"], 6.0))
    steps = oc_steps(loop, seconds)
    while len(steps) > MAX_STEPS:
        seconds *= 0.9 * MAX_STEPS / len(steps)
        steps = oc_steps(loop, seconds)
    return seconds, steps


def loops(s):
    import control
    rig = s.rig
    ctl = control.Control(rig)
    if rig.virtual:
        rig.oc.jacks.update({"pitch": ["v_oct"], "level": ["level"], "trigger": ["trigger"]})
        rig.detached.discard("trigger")
    ctl.engine(0, "split")
    ctl.telemetry(0)
    for name, value in KNOBS.items():
        ctl.knob(name, value)
    clips = []
    knobs = dict(KNOBS)
    for loop in page_loops():
        seconds, steps = fitting(loop)
        for name, value in PRESETS.get(loop["model"], {}).items():
            knobs[name] = value
            ctl.knob(name, value)
        ctl.engine(loop["model"], "stereo")
        # The page unpatches TRIGGER for the loops without triggers (the gate stays open).
        ctl.patched(trigger=None if loop["gate"] else False)
        for role in ("pitch", "level", "trigger", "timbre", "morph", "harmonics"):
            first = next((st for st in steps if st[1] == role), None)
            rig.oc.set(role, first[2] if first and first[0] == 0 else
                       (BASE if role in ("pitch", "level") else 0.0))
        rig.sleep(0.5)
        ctl.reset()
        rig.oc.sequence(steps)
        cap, _ = s.capture(tour.LEAD + seconds + tour.TAIL, tag=f"loop-{loop['number']}")
        f = None
        for attempt in range(2):
            try:
                f = ctl.frame(check=False, light=(7, "split"), tries=4)
                break
            except RuntimeError as e:
                s.say(f"PROBLEM: loop {loop['number']}: {e}")
        engine = loop["model"]
        start = int(tour.LEAD * 0.5 * cap.sr)
        y = cap.y[start:].copy()
        k = int(0.4 * cap.sr)
        y[-k:] *= np.linspace(1, 0, k)[:, None]
        info = {"number": loop["number"], "id": loop["id"], "name": loop["name"],
                "desc": loop["desc"], "model": engine, "seconds": seconds, "steps": len(steps),
                "knobs": dict(knobs),
                "peak": np.abs(cap.y).max(0).tolist(),
                "cycles": f.engine_cycles[engine] if f else 0,
                "overruns": f.engine_overruns[engine] if f else 0}
        s.say(f"loop {loop['number']} ({loop['name']}, model {engine + 1}): {seconds:.1f}s, "
              f"{len(steps)} steps, cycles {info['cycles']} "
              f"({info['cycles'] / telemetry.BLOCK_CYCLES * 100:.0f}%), overruns "
              f"{info['overruns']}, peaks {info['peak'][0]:.3f} {info['peak'][1]:.3f}")
        clips.append((info, y, cap.sr))
    ctl.patched(trigger=None)
    for role in ("pitch", "level", "trigger", "timbre", "morph", "harmonics"):
        rig.oc.set(role, 0.0)
    for name in KNOBS:
        ctl.knob(name, None)
    ctl.engine(0, "split")
    ctl.telemetry(control.DEFAULT_INTERVAL)
    result = assemble(clips, s.out)
    s.say(f"loops: {result['seconds']:.0f}s, {result['m4a']}")
    return result


def assemble(clips, out_dir):
    """loops.wav and loops.m4a in out_dir, from the clips [(info, y, sr)]."""
    from fractions import Fraction
    import scipy.io.wavfile as wavfile
    from scipy.signal import resample_poly
    sr_out = 48000
    out_dir = Path(out_dir)
    peaks = max(np.abs(y).max() for _, y, _ in clips)
    rms = np.median([np.sqrt(np.mean(y ** 2)) for _, y, _ in clips])
    gain = min(10 ** (-3 / 20) / peaks, 10 ** (-18 / 20) / rms)
    parts = [tour.speak("Plaits duo, recorded from the module, playing the loops of the web "
                        "demo. Two voices, in the stereo layout: the melody on V OCT, the "
                        "countermelody on LEVEL.", sr_out), tour.silence(0.8, sr_out)]
    for info, y, sr in clips:
        if sr != sr_out:
            q = Fraction(int(sr_out), int(round(sr)))
            y = resample_poly(y, q.numerator, q.denominator, axis=0)
        text = (f"Loop {tour_numeral(info['number'])}: {info['name']}, on the "
                f"{tour.MODELS[info['model']]} model. {info['desc']}")
        if info["overruns"]:
            text += " This one overruns the processor: the clicks are missed audio blocks."
        parts += [tour.speak(text, sr_out), tour.silence(0.4, sr_out),
                  tour.faded(y * gain, sr_out), tour.silence(0.8, sr_out)]
    parts.append(tour.speak("End of the loops.", sr_out))
    out = np.concatenate(parts)
    wav = out_dir / "loops.wav"
    wavfile.write(wav, sr_out, (np.clip(out, -1, 1) * 32767).astype(np.int16))
    m4a = out_dir / "loops.m4a"
    subprocess.run(["afconvert", "-f", "m4af", "-d", "aac", "-b", "192000", str(wav), str(m4a)],
                   check=True)
    return {"clips": [info for info, _, _ in clips], "gain_db": 20 * np.log10(gain),
            "seconds": len(out) / sr_out, "wav": str(wav), "m4a": str(m4a)}


def tour_numeral(n):
    return ["one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
            "ten"][n - 1]
