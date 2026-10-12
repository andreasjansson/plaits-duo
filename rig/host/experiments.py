"""The experiments of the Plaits Duo rig (docs/device-debugging-workflow.md).

Each takes a Session and returns a dict, which the session saves as JSON (with the raw
captures as .npz) in its results folder. All of them use the same wiring (rig/config.json):
one oC output into both V/OCT and LEVEL through a mult, so the two voices get exactly the
same voltage, and their interval measures LEVEL against V/OCT (whose factory calibration is
the reference), with no accurate voltage source.

  check     the module's state and the wiring, from the telemetry
  levelcal  the error of LEVEL against V/OCT, from -0.5V to 6V: uncalibrated (as V/OCT, and
            as the earlier firmware read it), and calibrated
  floor     the lowest voltage LEVEL reads (the op-amp's swing, the reference)
  noise     the jitter of the readings and of the two voices' pitches
  selfcal   the calibration against V/OCT (guided: OUT into V/OCT, then LEVEL)
  cpu       the most cycles per block of each model, both layouts, with and without triggers
  compare   each model on the module against the simulator, with the same knobs and CVs
  tour      every model in both layouts playing a phrase, with spoken announcements (tour.py)
"""
import json
import math
import time
from pathlib import Path

import numpy as np

import pitch
import telemetry

SETTLE = 2.3     # after a change: the next frame's 2s are all after it
LENGTH = 2.3     # a capture holds at least one frame
SEMITONES_PER_UNIT = 60.0
DEFAULT_LEVEL_NOTE_SCALE = 100.0


class Session:
    def __init__(self, rig, cfg, out_dir):
        self.rig = rig
        self.cfg = cfg
        self.out = Path(out_dir)
        self.out.mkdir(parents=True, exist_ok=True)
        self.n = 0
        self.log = []

    def say(self, text):
        line = f"[{time.strftime('%H:%M:%S')}] {text}"
        print(line, flush=True)
        self.log.append(line)

    def capture(self, seconds=LENGTH, tag=None):
        cap = self.rig.capture(seconds)
        frames = telemetry.decode(cap.aux, cap.sr)
        self.n += 1
        if tag:
            cap.save(self.out / f"{self.n:03d}-{tag}.npz")
        return cap, frames

    def measure(self, tag=None, settle=SETTLE, voices=True, ref=None):
        """After a change: wait, record, decode. The last frame's averages are all after the
        change. With voices, the pitch of OUT and AUX (without the telemetry on AUX): AUX at
        the partial of OUT's estimate, shifted by the interval the telemetry gives between the
        voices, and OUT at that of an earlier measurement (ref) shifted the same way, so
        that a waveform with strong harmonics is always measured at the same partial."""
        self.rig.sleep(settle)
        cap, frames = self.capture(tag=tag)
        m = {"frames": frames, "frame": frames[-1] if frames else None}
        if voices:
            keep = telemetry.mask(len(cap.aux), cap.sr, frames)
            for span in telemetry.bursts(cap.aux, cap.sr):
                a, b = int((span[0] - 0.005) * cap.sr), int((span[1] + 0.005) * cap.sr)
                keep[max(0, a):max(0, b)] = False
            f = m["frame"]
            near = None
            if ref and ref.get("f_out") and ref.get("frame") and f:
                near = ref["f_out"] * 2 ** ((f.note - ref["frame"].note) / 12)
            m["f_out"] = pitch.frequency(cap.out, cap.sr, near=near)
            near = m["f_out"] * 2 ** ((f.level_note - f.note) / 12) if m["f_out"] and f else None
            m["f_aux"] = pitch.frequency(cap.aux, cap.sr, keep, near=near)
            m["keep"] = float(keep.mean())
        return m

    def frame(self, tag=None):
        """The current state, from one frame."""
        m = self.measure(tag=tag, settle=0.1, voices=False)
        if m["frame"] is None:
            raise RuntimeError("no telemetry on AUX: is the rig firmware on the module, and AUX "
                               "on the input set in rig/config.json?")
        return m["frame"]

    def save(self, name, data):
        path = self.out / f"{name}.json"
        path.write_text(json.dumps(data, indent=1, default=_json))
        return path


def _json(o):
    if isinstance(o, (np.floating, np.integer)):
        return o.item()
    if isinstance(o, np.ndarray):
        return o.tolist()
    if isinstance(o, telemetry.Frame):
        return dict(o.data, t=o.t, margin=o.margin)
    return str(o)


# ---------- the calibrations of LEVEL, as functions of a reading ----------

def v_oct_volts(frame, x):
    """V/OCT's reading as volts, with its calibration (the reference)."""
    s, o = frame.v_oct_calibration
    return (x * s + o) / 12.0


def v_oct_scale(frame):
    """V/OCT's scale (semitones per unit of reading), which LEVEL borrows (as the firmware's
    VOctScale: the nominal -60 if implausible)."""
    s = frame.v_oct_calibration[0]
    return s if -66 < s < -54 else -60.0


def level_notes(frame, x):
    """The note of voice 2 from LEVEL's reading x: as the earlier firmware read it (the
    nominal gain), out of the box (V/OCT's scale), and calibrated against V/OCT."""
    s, o = frame.level_calibration
    level = x * s + o
    notes = {"nominal": level * DEFAULT_LEVEL_NOTE_SCALE,
             "v_oct": level * v_oct_scale(frame) / s}
    ns, no = frame.level_note_calibration
    if ns != 0:
        notes["calibrated"] = x * ns + no
    return notes


# ---------- experiments ----------

def check(s):
    """The module's state and the wiring."""
    rig, cfg = s.rig, s.cfg
    for role in cfg["oc"]:
        rig.oc.set(role, 0.0)
    s.rig.ask("Put the module in duo mode (both buttons together), split layout (right button: "
              "red LED), model 1 (classic waveforms: the top LED). Knobs as in rig/config.json.",
              sim=["tap both", "tap right"])
    f0 = s.frame("check-0V")
    s.say(f0.summary())
    problems = []
    if not f0.duo_mode:
        problems.append("not in duo mode")
    if f0.layout != "split" or f0.duo_engine & 7:
        problems.append("not the split layout's first model (classic waveforms)")
    if not f0.is_patched("level"):
        problems.append("nothing patched into LEVEL (oC A, through a mult)")
    m0 = s.measure("check-0V-voices")
    rig.oc.set("pitch", 2.0)
    m2 = s.measure("check-2V", ref=m0)
    f2 = m2["frame"]
    dv = f2.cv_mean(0) - m0["frame"].cv_mean(0)
    dl = f2.cv_mean(1) - m0["frame"].cv_mean(1)
    s.say(f"0V -> 2V: V/OCT reading {dv:+.4f}, LEVEL {dl:+.4f} (nominal -0.4 each)")
    if abs(dv + 0.4) > 0.08:
        problems.append("V/OCT does not follow oC A")
    if abs(dl + 0.4) > 0.08:
        problems.append("LEVEL does not follow oC A")
    voices = {}
    for name, m in (("0V", m0), ("2V", m2)):
        voices[name] = {"out_hz": m["f_out"], "aux_hz": m["f_aux"]}
    if m0["f_out"] and m2["f_out"]:
        st = 12 * math.log2(m2["f_out"] / m0["f_out"])
        s.say(f"voice 1: {m0['f_out']:.2f}Hz -> {m2['f_out']:.2f}Hz ({st:+.2f} semitones)")
    if m0["f_aux"] and m2["f_aux"]:
        st = 12 * math.log2(m2["f_aux"] / m0["f_aux"])
        s.say(f"voice 2: {m0['f_aux']:.2f}Hz -> {m2['f_aux']:.2f}Hz ({st:+.2f} semitones)")
    # MODEL CV: one model up
    rig.oc.set("pitch", 0.0)
    rig.oc.set("model", cfg["model_volts"])
    fm = s.measure("check-model", voices=False)["frame"]
    rig.oc.set("model", 0.0)
    if fm is None:
        problems.append("no telemetry in the MODEL CV capture")
    else:
        s.say(f"MODEL at {cfg['model_volts']}V: active model {fm.active_engine & 7} (expected 1)")
        if fm.active_engine & 7 != 1:
            problems.append("MODEL CV does not select the next model (oC C into MODEL?)")
    for p in problems:
        s.say("PROBLEM: " + p)
    return {"frame": f0, "reading_change_2V": [dv, dl], "voices": voices,
            "model_cv_active": fm.active_engine, "problems": problems,
            "margin": min(f.margin for f in m0["frames"] + m2["frames"])}


def levelcal(s, volts=(-0.5, 0.0, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0), tag="levelcal"):
    """LEVEL against V/OCT: the same voltage into both, read by each, and the pitches of the
    two voices (split layout: voice 1 on OUT, voice 2 on AUX)."""
    rig = s.rig
    points = []
    for v in volts:
        rig.oc.set("pitch", v)
        m = s.measure(f"{tag}-{v:+.2f}V")
        f = m["frame"]
        if f is None:
            s.say(f"{v:+.2f}V: no telemetry")
            continue
        p = {"oc_volts": v, "x_v_oct": f.cv_mean(0), "x_level": f.cv_mean(1),
             "std_v_oct": f.cv_std(0), "std_level": f.cv_std(1),
             "note": f.note, "level_note": f.level_note,
             "f_out": m["f_out"], "f_aux": m["f_aux"]}
        p["volts"] = v_oct_volts(f, p["x_v_oct"])
        if m["f_out"] and m["f_aux"]:
            p["interval_cents"] = float(pitch.cents(m["f_aux"], m["f_out"]))
        p["predicted_cents"] = (f.level_note - f.note) * 100
        points.append(p)
        s.say(f"{v:+.2f}V: V/OCT reads {p['volts']:+.4f}V; voice 2 - voice 1: "
              f"{p.get('interval_cents', float('nan')):+.2f} cents measured, "
              f"{p['predicted_cents']:+.2f} from the telemetry")
    rig.oc.set("pitch", 0.0)
    last = f
    # LEVEL's readings against the volts V/OCT reads, where LEVEL does not clip
    good = [p for p in points if p["volts"] > -0.45]
    V = np.array([p["volts"] for p in good])
    X = np.array([p["x_level"] for p in good])
    b, a = np.polyfit(V, X, 1)
    result = {"points": points, "level_zero": a, "level_per_volt": b}
    # LEVEL's gain against V/OCT's (the difference of their resistors' tolerances), and the
    # converter's against its nominal 0.2 per volt (shared by the two inputs)
    v_oct_per_volt = -12.0 / v_oct_scale(last)
    result["level_against_v_oct"] = abs(b) / v_oct_per_volt - 1
    result["v_oct_against_nominal"] = v_oct_per_volt / 0.2 - 1
    s.say(f"LEVEL reads {b:+.6f} per volt, V/OCT {-v_oct_per_volt:+.6f}: LEVEL "
          f"{result['level_against_v_oct'] * 100:+.3f}% against V/OCT, V/OCT "
          f"{result['v_oct_against_nominal'] * 100:+.3f}% against the nominal gain")
    # each calibration's error, in cents, against the volts V/OCT reads
    errors = {}
    for method in level_notes(last, 0.0):
        e = np.array([level_notes(last, x)[method] - 12 * v for x, v in zip(X, V)]) * 100
        slope, offset = np.polyfit(V, e, 1)
        errors[method] = {"cents_at_0V": offset, "cents_per_octave": slope,
                          "max_cents_0_to_5V": float(np.max(np.abs(e[(V >= 0) & (V <= 5.01)])))}
        s.say(f"{method}: {offset:+.2f} cents at 0V, {slope:+.2f} cents per octave, "
              f"at most {errors[method]['max_cents_0_to_5V']:.2f} cents from 0 to 5V")
    result["errors"] = errors
    measured = [p for p in points if "interval_cents" in p]
    if measured:
        d = [p["interval_cents"] - p["predicted_cents"] for p in measured]
        s.say(f"audio against telemetry: {np.mean(d):+.2f} cents on average, "
              f"{np.max(np.abs(d)):.2f} at most")
        result["audio_minus_telemetry_cents"] = d
    result["active"] = "calibrated" if last.level_note_calibration[0] else "v_oct"
    result["frame"] = last
    return result


def floor(s, start=-0.3, stop=-1.3, step=-0.05):
    """The lowest voltage LEVEL reads: where its reading stops following V/OCT's."""
    rig = s.rig
    points = []
    v = start
    while v >= stop - 1e-9:
        rig.oc.set("pitch", v)
        m = s.measure(f"floor-{v:+.2f}V")
        f = m["frame"]
        if f:
            points.append({"oc_volts": v, "volts": v_oct_volts(f, f.cv_mean(0)),
                           "x_level": f.cv_mean(1), "level_note": f.level_note,
                           "note": f.note, "f_out": m["f_out"], "f_aux": m["f_aux"]})
        v += step
    rig.oc.set("pitch", 0.0)
    V = np.array([p["volts"] for p in points])
    X = np.array([p["x_level"] for p in points])
    # the slope above -0.5V, and the first point which falls short of it by 1%
    ref = V > -0.5
    b, a = np.polyfit(V[ref], X[ref], 1) if ref.sum() >= 2 else (-0.2, 0.0)
    clipped = [p for p in points if (p["x_level"] - (a + b * p["volts"])) < -0.01 * abs(b)]
    floor_volts = clipped[0]["volts"] if clipped else None
    if floor_volts is not None:
        s.say(f"LEVEL stops at {floor_volts:+.3f}V: voice 2 goes at most "
              f"{-min(p['level_note'] for p in points):.2f} semitones below FREQUENCY")
    else:
        s.say("LEVEL followed V/OCT down to the lowest voltage tried")
    return {"points": points, "floor_volts": floor_volts,
            "lowest_note": min(p["level_note"] for p in points) if points else None}


def noise(s, volts=(1.0, 4.0), seconds=6.0):
    """The jitter of the readings (from the telemetry) and of the voices' pitches."""
    rig = s.rig
    out = []
    for v in volts:
        rig.oc.set("pitch", v)
        rig.sleep(SETTLE)
        cap, frames = s.capture(seconds, tag=f"noise-{v:+.1f}V")
        keep = telemetry.mask(len(cap.aux), cap.sr, frames)
        f1 = pitch.frequency(cap.out, cap.sr)
        f2 = pitch.frequency(cap.aux, cap.sr, keep)
        t1 = pitch.track(cap.out, cap.sr, f1) if f1 else []
        t2 = pitch.track(cap.aux, cap.sr, f2, keep) if f2 else []
        j1 = float(np.std(pitch.cents(np.array([f for _, f in t1]), f1))) if t1 else None
        j2 = float(np.std(pitch.cents(np.array([f for _, f in t2]), f2))) if t2 else None
        fr = frames[-1] if frames else None
        r = {"volts": v, "frames": len(frames),
             "reading_std_cents": [fr.cv_std(0) * SEMITONES_PER_UNIT * 100,
                                   fr.cv_std(1) * SEMITONES_PER_UNIT * 100] if fr else None,
             "pitch_jitter_cents": [j1, j2]}
        s.say(f"{v:+.1f}V: readings V/OCT {r['reading_std_cents'][0]:.2f}, LEVEL "
              f"{r['reading_std_cents'][1]:.2f} cents rms (before smoothing); pitch jitter "
              f"voice 1 {j1 or float('nan'):.3f}, voice 2 {j2 or float('nan'):.3f} cents rms (50ms)")
        out.append(r)
    rig.oc.set("pitch", 0.0)
    return {"points": out}


def selfcal(s):
    """The calibration of LEVEL against V/OCT, without a voltage source (guided)."""
    rig = s.rig
    rig.oc.set("pitch", 0.0)
    rig.ask("Hold both buttons for 2 seconds: the bottom LED breathes green.",
            sim=["press both", "wait 2100ms", "release both", "wait 300ms"])
    rig.ask("Patch OUT into V/OCT (instead of the oC), then press the left button: the bottom "
            "LED turns yellow. (With a stackable cable or a mult into LEVEL too, this press "
            "ends the calibration.)",
            sim=["patch v_oct main", "wait 600ms", "tap left"])
    rig.ask("Move that cable from V/OCT to LEVEL, then press the left button: the LEDs show "
            "the model again (all red: rejected).",
            sim=["unpatch v_oct", "patch level main", "wait 600ms", "tap left"])
    rig.ask("Restore the wiring: oC A through the mult into V/OCT and LEVEL, OUT into the "
            "audio interface.", sim=["@replug v_oct", "@replug level"])
    f = s.frame("selfcal")
    s.say(f.summary())
    scale, offset = f.level_note_calibration
    s.say(f"LEVEL calibrated: {scale:.4f} semitones per unit (nominal -60), offset "
          f"{offset:.4f}" if scale else "PROBLEM: no calibration stored")
    return {"frame": f, "scale": scale, "offset": offset, "ok": bool(scale)}


def cpu(s, loops=1):
    """The most cycles per block of each model, both layouts, two voices, playing the
    tour's phrase (rig/host/tour.py: notes on V/OCT and LEVEL, pinged on TRIGGER, TIMBRE
    rising) with the tour's knobs, set by command; and the most cycles of each section of the
    voice. (The rig firmware's commands: the statistics of each model are read with a light
    model, as a model which overruns garbles the telemetry.)"""
    import control
    import tour
    rig = s.rig
    ctl = control.Control(rig)
    if rig.virtual:
        rig.oc.jacks.update({"pitch": ["v_oct"], "level": ["level"], "trigger": ["trigger"]})
        rig.detached.discard("trigger")
    ctl.engine(7, "split")
    ctl.telemetry(0)
    for name, value in tour.KNOBS.items():
        ctl.knob(name, value)
    rig.oc.set("morph", tour.MORPH)
    first = ctl.frame(check=False, light=(7, "split"))
    s.say(first.summary())
    poll = first.poll_cycles
    phrase = [step for step in tour.steps()
              if step[0] < (tour.LEAD + loops * tour.LOOP) * 1000]
    results = []
    for layout in ("split", "stereo"):
        for model in range(8):
            engine = model | (8 if layout == "split" else 0)
            ctl.engine(model, layout)
            rig.sleep(0.3)
            ctl.reset()
            rig.oc.sequence(phrase)
            rig.sleep(tour.LEAD + loops * tour.LOOP + 0.3)
            ctl.reset(freeze=True)
            try:
                f = ctl.frame(check=False, light=(7, "split"))
            except RuntimeError as e:
                s.say(f"PROBLEM: {layout} model {model + 1}: {e}")
                continue
            r = {"layout": layout, "model": model, "cycles": f.engine_cycles[engine],
                 "overruns": f.engine_overruns[engine], "sections": f.section_cycles,
                 "poll": f.poll_cycles}
            results.append(r)
            s.say(f"{layout:6s} {model + 1} {tour.MODELS[model]:18s} {r['cycles']:6d} cycles "
                  f"({r['cycles'] / telemetry.BLOCK_CYCLES * 100:5.1f}%), overruns "
                  f"{r['overruns']:5d}; sections {'/'.join(map(str, r['sections']))}")
    for name in tour.KNOBS:
        ctl.knob(name, None)
    ctl.telemetry(control.DEFAULT_INTERVAL)
    return {"results": results, "poll": poll}


def compare(s, volts=1.0, seconds=3.0):
    """Each model of both layouts on the module, against plaits-sim given the same knobs (read
    from the telemetry) and CVs: the level of each output, the L/R correlation and the
    spectrum. With one voice (LEVEL unplugged): two voices in unison would add up differently
    on each run, depending on their phases."""
    import subprocess
    import tempfile
    import scipy.io.wavfile as wavfile
    from rigio import ROOT
    sim = ROOT / "build/host/plaits-sim"
    rig, cfg = s.rig, s.cfg
    rig.oc.set("pitch", volts)
    for role in ("harmonics", "timbre", "morph"):
        rig.oc.set(role, 0.0)
    rig.ask("Pull the cable out of LEVEL (one voice), and patch oC B into TRIGGER (the string "
            "and speech models need pings to sound the same on each run).",
            sim=["unpatch level", "@replug trigger"])
    rate = 4.0
    out = []
    for layout in ("split", "stereo"):
        if layout == "stereo":
            rig.ask("Select the stereo layout (left button once).", sim=["tap left"])
        for model in range(8):
            rig.oc.set("model", model * cfg["model_volts"])
            rig.oc.trigger_train("trigger", rate, SETTLE + seconds + 0.5)
            rig.sleep(SETTLE)
            cap, frames = s.capture(seconds, tag=f"compare-{layout}-{model + 1}")
            if not frames:
                s.say(f"{layout} model {model + 1}: no telemetry")
                continue
            f = frames[-1]
            keep = telemetry.mask(len(cap.aux), cap.sr, frames)
            # the same on the simulator: duo mode, the layout, the knobs and the CVs
            script = ["wait 500ms", "tap both"] + (["tap right"] if layout == "split" else [])
            script += [f"knob {n} {p / 65536.0:.5f}" for n, p in zip(telemetry.POTS, f.pots)]
            script += [f"cv v_oct {volts}", f"cv model {model * cfg['model_volts']:.4f}",
                       "cv harmonics 0", "cv timbre 0", "cv morph 0"]
            pings = ["trigger", f"wait {1000 / rate - 10:.0f}ms"]
            script += pings * int(2 * rate) + ["clear"] + pings * int(seconds * rate)
            with tempfile.TemporaryDirectory() as td:
                path = Path(td) / "sim.wav"
                subprocess.run([str(sim), "-o", str(path)], input="\n".join(script) + "\n",
                               text=True, check=True, capture_output=True)
                sr2, y2 = wavfile.read(path)
            y2 = y2.astype(np.float64) / 32768.0
            a, b = _stats(cap.y, cap.sr, keep), _stats(y2, sr2, None)
            r = {"layout": layout, "model": model, "active": f.active_engine, "module": a, "sim": b,
                 "level_db": [x - y for x, y in zip(a["rms_db"], b["rms_db"])],
                 "spectrum_db": float(np.max(np.abs(np.array(a["bands_db"]) - np.array(b["bands_db"]))))}
            out.append(r)
            s.say(f"{layout} model {model + 1}: module - sim {r['level_db'][0]:+.2f} dB (OUT), "
                  f"{r['level_db'][1]:+.2f} dB (AUX); L/R correlation {a['correlation']:+.3f} "
                  f"(sim {b['correlation']:+.3f}); spectra within {r['spectrum_db']:.1f} dB")
        rig.oc.set("model", 0.0)
    rig.ask("Select the split layout again (right button), pull the cable out of TRIGGER, "
            "and plug the cable back into LEVEL.",
            sim=["tap right", "unpatch trigger", "@replug level"])
    rig.oc.set("pitch", 0.0)
    return {"results": out}


def _stats(y, sr, keep):
    """Levels, correlation and third-octave spectrum (of L + R, relative to its total) of a
    stereo recording, over the kept samples (None: all)."""
    if keep is not None:
        y = y[keep]
    y = y - y.mean(0)
    rms = np.sqrt(np.mean(y * y, 0)) + 1e-12
    corr = float(np.corrcoef(y[:, 0], y[:, 1])[0, 1]) if np.all(rms > 1e-6) else 1.0
    spec = np.abs(np.fft.rfft(y[:, 0] + y[:, 1])) ** 2
    fr = np.fft.rfftfreq(len(y), 1 / sr)
    edges = 50 * 2 ** (np.arange(0, 28) / 3)
    total = spec.sum() + 1e-12
    bands = [10 * np.log10(spec[(fr >= lo) & (fr < hi)].sum() / total + 1e-9)
             for lo, hi in zip(edges[:-1], edges[1:])]
    return {"rms_db": (20 * np.log10(rms)).tolist(), "correlation": corr, "bands_db": bands}


from tour import tour  # noqa: E402
from loops import loops  # noqa: E402

EXPERIMENTS = {"check": check, "levelcal": levelcal, "floor": floor,
               "noise": noise, "selfcal": selfcal, "cpu": cpu, "compare": compare,
               "tour": tour, "loops": loops}

# A whole session: the module as it is (uncalibrated, or as calibrated before), then
# calibrated against V/OCT, then the CPU and the comparison with the simulator.
SESSION = ["check", "levelcal", "floor", "noise", "selfcal",
           ("levelcal", "levelcal-after-selfcal"), "cpu", "compare"]
