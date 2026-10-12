"""A listening tour: every duo model in both layouts, recorded from the module, with spoken
announcements between the clips.

Each clip is the same phrase: 12 notes in D dorian, played three times, voice 1 (V/OCT) the
melody and voice 2 (LEVEL) a diatonic third below, each note pinged on TRIGGER, TIMBRE rising
slowly. The oC times the notes. The knobs, the models and the layouts are set by the rig
firmware's commands, and the telemetry only comes between the clips. (The captures of the rig
firmware's version 1 had telemetry bursts on AUX, replaced by the same moment of the phrase
one loop away: reassemble() makes the tour again from them.)

Wiring (rig/config.json): oC jack A into V/OCT (role pitch), oC B into LEVEL (role level), oC G
into TRIGGER (role trigger), the other roles as usual.

Writes tour.wav and tour.m4a (AAC) in the results folder, and every clip as .npz.
"""
import subprocess
import tempfile
import warnings
from fractions import Fraction
from pathlib import Path

import numpy as np
import scipy.io.wavfile as wavfile
from scipy.signal import resample_poly

import telemetry

MODELS = ["classic waveforms", "waveshaper", "two-operator FM", "formant", "harmonic",
          "wavetable", "string", "vowel"]
SCALE = [0, 2, 3, 5, 7, 9, 10]                      # dorian
MELODY = [4, 7, 6, 4, 3, 4, 2, 3, 1, 2, 0, 1]       # scale degrees above D
HARMONY = -2                                        # a third below, in the scale
NOTE = 0.25                                         # seconds per note
LOOPS = 3
LOOP = NOTE * len(MELODY)
BASE = 2.0                                          # volts of D: about D3 with FREQUENCY at 9 o'clock
LEAD = 0.4                                          # seconds from the oC's start to the first note (the
                                                    # recording starts a little later on the module)
TAIL = 0.8                                          # after the last note
TIMBRE = (1.0, 3.5)                                 # volts, rising during a clip
MORPH = 1.0
SPEECH_VOICE, SPEECH_RATE = "Samantha", 175


def semitones(degree):
    return 12 * (degree // 7) + SCALE[degree % 7]


def steps():
    """The phrase, as oC steps (t_ms, role, volts[, glide_ms])."""
    out = [(0, "timbre", TIMBRE[0]),
           (LEAD * 1000, "timbre", TIMBRE[1], LOOPS * LOOP * 1000)]
    for k in range(LOOPS):
        for i, d in enumerate(MELODY):
            t = (LEAD + (k * len(MELODY) + i) * NOTE) * 1000
            out.append((t, "pitch", BASE + semitones(d) / 12))
            out.append((t, "level", BASE + semitones(d + HARMONY) / 12))
            out.append((t + 2, "trigger", 5.0))
            out.append((t + 12, "trigger", 0.0))
    return out


# ---------- the telemetry bursts on AUX ----------

def burst_mask(x, sr, frames):
    """True where AUX carries telemetry: the decoded frames, and those cut off at the ends
    (they come every 8000 blocks). When no frame decodes, or the frames report overruns (which
    make the blocks irregular): also anything that correlates like a frame, and stretches
    whose energy is almost all between 3 and 9 kHz and spread over the band (the Manchester
    code; a high harmonic is a narrow peak), extended to a whole frame."""
    n = len(x)
    bad = ~telemetry.mask(n, sr, frames, guard=0.01) if frames else np.zeros(n, bool)
    if frames and not any(f.overruns for f in frames):
        return bad
    for a, b in telemetry.bursts(x, sr):
        bad[max(0, int((a - 0.01) * sr)):max(0, int((b + 0.01) * sr))] = True
    win, hop = 256, 128
    fr = np.fft.rfftfreq(win, 1 / sr)
    band = (fr > 3000) & (fr < 9000)
    w = np.hanning(win)
    starts = np.arange(0, n - win, hop)
    hot = np.zeros(len(starts), bool)
    for j, i in enumerate(starts):
        seg = x[i:i + win] - np.mean(x[i:i + win])
        p = np.abs(np.fft.rfft(seg * w)) ** 2
        total, inband = p.sum(), p[band]
        peak = np.convolve(inband, np.ones(5), "valid").max() if inband.size >= 5 else 0
        hot[j] = (total > 1e-6 * win and inband.sum() > 0.8 * total and peak < 0.6 * inband.sum()
                  and np.sqrt(np.mean(seg ** 2)) > 0.15)
    length = telemetry.BITS * telemetry.SAMPLES_PER_BIT / telemetry.MODULE_RATE
    j = 0
    while j < len(hot):
        if hot[j]:
            k = j
            while k + 1 < len(hot) and hot[k + 1]:
                k += 1
            if (k - j + 1) * hop / sr > 0.01:
                a = starts[j] / sr - 0.01
                b = max(starts[k] + win, starts[j] + length * sr) / sr + 0.01
                bad[max(0, int(a * sr)):min(n, int(b * sr))] = True
            j = k + 1
        else:
            j += 1
    return bad


def fill(x, sr, bad, loop, fade=0.008):
    """x with each bad span replaced by the same moment one or two loops away (a span there
    clear of bursts), crossfaded. Returns the filled signal and the spans."""
    y = x.copy()
    n, L, xf = len(x), int(round(loop * sr)), int(fade * sr)
    edges = np.flatnonzero(np.diff(np.concatenate([[0], bad.astype(int), [0]])))
    spans = list(zip(edges[::2], edges[1::2]))
    for a, b in spans:
        a0, b0 = max(0, a - xf), min(n, b + xf)
        src = None
        for d in (-L, L, -2 * L, 2 * L):
            if 0 <= a0 + d and b0 + d <= n and not bad[a0 + d:b0 + d].any():
                src = x[a0 + d:b0 + d]
                break
        if src is None:
            src = np.zeros(b0 - a0)
        w = np.ones(b0 - a0)
        ramp = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, xf))
        w[:a - a0] = ramp[:a - a0]
        if b0 - b:
            w[-(b0 - b):] = ramp[::-1][:b0 - b]
        y[a0:b0] = (1 - w) * x[a0:b0] + w * src
    return y, spans


def first_onset(y, sr, search=1.5):
    """Seconds: the first note (the LPG opening) in a capture."""
    x = np.abs(y[:int(search * sr)]).sum(1)
    env = np.convolve(x, np.ones(int(0.005 * sr)) / int(0.005 * sr), "same")
    return float(np.flatnonzero(env > 0.3 * env.max())[0] / sr)


# ---------- speech and assembly ----------

def speak(text, sr):
    with tempfile.TemporaryDirectory() as td:
        path = Path(td) / "say.wav"
        subprocess.run(["say", "-v", SPEECH_VOICE, "-r", str(SPEECH_RATE), "-o", str(path),
                        "--file-format=WAVE", f"--data-format=LEI16@{int(sr)}", text], check=True)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", wavfile.WavFileWarning)
            r, y = wavfile.read(path)
    y = y.astype(np.float64) / 32768.0
    if y.ndim > 1:
        y = y.mean(1)
    if r != sr:
        raise RuntimeError(f"say gave {r}Hz, not {sr}Hz")
    rms = np.sqrt(np.mean(y[np.abs(y) > 1e-3] ** 2)) + 1e-9
    return np.stack([y, y], 1) * (10 ** (-20 / 20) / rms)


def silence(seconds, sr):
    return np.zeros((int(seconds * sr), 2))


def faded(y, sr, fade_in=0.01):
    y = y.copy()
    a = int(fade_in * sr)
    y[:a] *= np.linspace(0, 1, a)[:, None]
    return y


# The knobs, set exactly by command (the same on the virtual module): FREQUENCY at about 9
# o'clock, HARMONICS at 12, TIMBRE fully counterclockwise, MORPH at about 11, the TIMBRE and
# MORPH attenuverters fully clockwise, the FM attenuverter centered.
KNOBS = {"frequency": 0.25, "harmonics": 0.5, "timbre": 0.0, "morph": 0.4,
         "timbre_cv": 1.0, "fm_cv": 0.5, "morph_cv": 1.0}


def tour(s):
    """With the rig firmware's commands (version 2 or later): the knobs, the models and the layouts
    are set by command, and the telemetry only comes after each clip (with the most cycles
    and the overruns of the model, during the clip)."""
    import control
    rig = s.rig
    ctl = control.Control(rig)
    if rig.virtual:
        # the tour's wiring: LEVEL on its own output, TRIGGER patched
        rig.oc.jacks.update({"pitch": ["v_oct"], "level": ["level"], "trigger": ["trigger"]})
        rig.detached.discard("trigger")
    for role in ("pitch", "level", "trigger", "harmonics"):
        rig.oc.set(role, 0.0)
    rig.oc.set("morph", MORPH)
    rig.oc.set("timbre", TIMBRE[0])
    ctl.engine(0, "split")      # a light model: frames are readable
    ctl.telemetry(0)
    for name, value in KNOBS.items():
        ctl.knob(name, value)
    f = ctl.frame(check=False, light=(0, "split"))
    s.say(f.summary())
    missing = [j for j in ("level", "trigger") if not f.is_patched(j)]
    if missing:
        rig.ask("Patch " + " and ".join({"level": "oC jack B into LEVEL",
                                         "trigger": "oC jack G into TRIGGER"}[j] for j in missing) + ".")
    clips = []
    phrase = steps()
    length = LEAD + LOOPS * LOOP + TAIL
    for layout in ("split", "stereo"):
        for model in range(8):
            ctl.engine(model, layout)
            ctl.reset()     # after the switch to the model: its cycles during the clip
            rig.oc.set("timbre", TIMBRE[0])
            rig.sleep(0.5)
            if not clips:
                rig.oc.sequence(phrase)
            else:
                rig.oc.replay()
            cap, _ = s.capture(length, tag=f"tour-{layout}-{model + 1}")
            # The frame with a light model (a model which overruns garbles the frames): the
            # statistics of each model are kept until reset.
            try:
                # the lightest model (60% on the module)
                f = ctl.frame(check=False, light=(7, "split"))
            except RuntimeError as e:
                s.say(f"PROBLEM: {layout} model {model + 1}: {e}")
                np.savez_compressed(s.out / f"failed-frame-{layout}-{model + 1}.npz",
                                    y=ctl.failed.y.astype(np.float32), sr=ctl.failed.sr)
                f = None
            engine = model | (8 if layout == "split" else 0)
            info, y = take(cap.y, cap.sr, layout, model)
            if f is not None:
                info.update({"cycles": f.engine_cycles[engine],
                             "overruns": f.engine_overruns[engine],
                             "sections": f.section_cycles, "active": f.active_engine})
            else:
                info.update({"cycles": 0, "overruns": 0, "sections": [0, 0, 0, 0]})
            info["suspect"] = info["overruns"] > 0

            s.say(f"{layout} model {model + 1} ({MODELS[model]}): cycles {info['cycles']} "
                  f"({info['cycles'] / telemetry.BLOCK_CYCLES * 100:.0f}%), overruns "
                  f"{info['overruns']}, sections {'/'.join(map(str, info['sections']))}, "
                  f"peaks {info['peak'][0]:.3f} {info['peak'][1]:.3f}")
            if max(info["peak"]) > 0.98:
                s.say("PROBLEM: the recording clips: turn the audio interface's gain down")
            clips.append((info, y, cap.sr))
    for role in ("pitch", "level", "trigger", "timbre", "morph"):
        rig.oc.set(role, 0.0)
    for name in KNOBS:
        ctl.knob(name, None)
    ctl.engine(0, "split")
    ctl.telemetry(control.DEFAULT_INTERVAL)
    result = assemble(clips, s.out)
    s.say(f"tour: {result['seconds']:.0f}s, {result['m4a']}")
    return result


def take(y, sr, layout, model):
    """A clip from a capture without telemetry: from just before its first note, faded out
    at its end."""
    start = first_onset(y, sr)
    out = y[max(0, int((start - 0.15) * sr)):].copy()
    k = int(0.3 * sr)
    out[-k:] *= np.linspace(1, 0, k)[:, None]
    info = {"layout": layout, "model": model, "peak": np.abs(y).max(0).tolist(),
            "rms_db": (20 * np.log10(np.sqrt(np.mean(out ** 2, 0)) + 1e-12)).tolist()}
    return info, out


def clip(y, sr, frames, layout, model):
    """(Captures of the rig firmware's version 1, with the telemetry every 2s.) A capture's
    AUX without the telemetry, and what the frames say. The first frame of a
    capture also covers the time before it (the frames count since the previous one, 2s
    earlier): the previous model, and the switch to this one. Only the frames entirely in the
    capture count; a capture with telemetry bursts but no frame decoded is one where the
    blocks are late (overruns garble the frames)."""
    bad = burst_mask(y[:, 1], sr, frames)
    aux, spans = fill(y[:, 1], sr, bad, LOOP)
    period = telemetry.FRAME_INTERVAL_BLOCKS * telemetry.BLOCK_SIZE / telemetry.MODULE_RATE
    inside = [f for f in frames if f.t > period + 0.05]
    got = frames[-1] if frames else None
    out = np.stack([y[:, 0], aux], 1)
    # One loop earlier is the same music only until the next loop's first note would come
    # (a note after the last one): a burst in the rest of the last note's decay ends the clip.
    start = first_onset(y, sr)
    limit = int((start + LOOPS * LOOP) * sr)
    end, fade = len(out), 0.3
    for a, b in spans:
        if b + int(0.008 * sr) > limit:
            end, fade = max(a, limit), 0.06
            break
    out = out[:end].copy()
    k = int(fade * sr)
    out[-k:] *= np.linspace(1, 0, k)[:, None]
    out = out[max(0, int((start - 0.15) * sr)):]
    info = {"layout": layout, "model": model, "frames": len(frames), "bursts": len(spans),
            "tail": (end - limit) / sr,
            "overruns": sum(f.overruns for f in inside),
            "overruns_before": sum(f.overruns for f in frames if f not in inside),
            "cycles": max((f.cycles for f in inside), default=None),
            "cycles_before": max((f.cycles for f in frames if f not in inside), default=None),
            "active": got.active_engine if got else None,
            "layout_seen": got.layout if got else None,
            "peak": np.abs(y).max(0).tolist(),
            "rms_db": (20 * np.log10(np.sqrt(np.mean(out ** 2, 0)) + 1e-12)).tolist()}
    info["suspect"] = info["overruns"] > 0 or (bool(spans) and not inside)
    return info, out


def assemble(clips, out_dir):
    """The tour from the clips [(info, y, sr)], as tour.wav and tour.m4a in out_dir."""
    sr_out = 48000
    out_dir = Path(out_dir)
    # one gain for all the clips (their levels stay comparable): the loudest peak at -3 dBFS,
    # the median clip no louder than -18 dBFS rms
    peaks = max(np.abs(y).max() for _, y, _ in clips)
    rms = np.median([np.sqrt(np.mean(y ** 2)) for _, y, _ in clips])
    gain = min(10 ** (-3 / 20) / peaks, 10 ** (-18 / 20) / rms)
    parts = [speak("Plaits duo, recorded from the module. Every model plays the same phrase: "
                   "two voices a third apart, pinged by triggers, with TIMBRE rising.", sr_out),
             silence(0.8, sr_out)]
    intro = {"split": "Split layout. Voice one, pitched by V OCT, on the left. Voice two, "
                      "pitched by LEVEL, on the right.",
             "stereo": "Stereo layout. Both voices, mixed in stereo."}
    for info, y, sr in clips:
        if sr != sr_out:       # the virtual module records at its own rate
            q = Fraction(int(sr_out), int(round(sr)))
            y = resample_poly(y, q.numerator, q.denominator, axis=0)
        if info["model"] == 0:
            parts += [speak(intro[info["layout"]], sr_out), silence(0.6, sr_out)]
        text = f"{info['layout'].capitalize()}, model {info['model'] + 1}: {MODELS[info['model']]}."
        if info["suspect"]:
            text += " This one overruns the processor: the clicks are missed audio blocks."
        parts += [speak(text, sr_out), silence(0.4, sr_out),
                  faded(y * gain, sr_out), silence(0.7, sr_out)]
    parts.append(speak("End of the tour.", sr_out))
    out = np.concatenate(parts)
    wav = out_dir / "tour.wav"
    wavfile.write(wav, sr_out, (np.clip(out, -1, 1) * 32767).astype(np.int16))
    m4a = out_dir / "tour.m4a"
    subprocess.run(["afconvert", "-f", "m4af", "-d", "aac", "-b", "192000", str(wav), str(m4a)],
                   check=True)
    return {"clips": [info for info, _, _ in clips], "gain_db": 20 * np.log10(gain),
            "seconds": len(out) / sr_out, "wav": str(wav), "m4a": str(m4a)}


def reassemble(out_dir):
    """The tour again, from the captures saved in a results folder."""
    import json
    import re
    clips = []
    for path in sorted(Path(out_dir).glob("*-tour-*-[0-9].npz")):
        layout, model = re.match(r"\d+-tour-(\w+)-(\d)\.npz", path.name).groups()
        d = np.load(path)
        sr = float(d["sr"])
        y = d["y"].astype(np.float64)
        info, y = clip(y, sr, telemetry.decode(y[:, 1], sr), layout, int(model) - 1)
        clips.append((info, y, sr))
    result = assemble(clips, out_dir)
    (Path(out_dir) / "tour.json").write_text(json.dumps(result, indent=1))
    return result


if __name__ == "__main__":
    import sys
    r = reassemble(sys.argv[1])
    for c in r["clips"]:
        print(f"{c['layout']:6s} {c['model'] + 1} {MODELS[c['model']]:18s} frames {c['frames']} "
              f"inside: cycles {c['cycles']} overruns {c['overruns']}"
              f"{'  OVERRUNS' if c['suspect'] else ''}")
    print(f"{r['seconds']:.0f}s, gain {r['gain_db']:+.1f} dB: {r['m4a']}")
