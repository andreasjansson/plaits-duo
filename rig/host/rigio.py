"""The rig: the module's outputs recorded by the audio interface, its CV inputs driven by the
oC (hardware), or the simulator of the rig firmware standing in for both (virtual).

    rig = make_rig(cfg, virtual=False)
    rig.oc.set("pitch", 2.0)
    rig.sleep(2.3)
    cap = rig.capture(2.3)           # cap.out, cap.aux: float arrays at cap.sr
    rig.ask("Pull the cable out of LEVEL.", sim=["unpatch level"])

Experiments only use oc, sleep, capture and ask, so they run unchanged on both. ask() shows
a request to the person at the module (and waits for Enter); on the virtual rig, it runs the
plaits-sim commands that do the same ("@replug JACK" patches JACK back to the oC output
of its role, at that output's voltage).

The virtual module is plaits-sim-rig (make -f plaits/test/host.mk), one process for the whole
session, with the hardware errors given by --hw (plaits-sim commands such as
"gain_error level 1.02" or "hw vbat_ratio 1.003", see plaits/test/plaits_sim.cc).
"""
import json
import os
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import scipy.io.wavfile as wavfile

ROOT = Path(__file__).resolve().parents[2]
SIM_RIG = ROOT / "build/host/plaits-sim-rig"


@dataclass
class Capture:
    y: np.ndarray       # (n, 2): OUT, AUX
    sr: float
    t: float            # rig time at the start

    @property
    def out(self):
        return self.y[:, 0]

    @property
    def aux(self):
        return self.y[:, 1]

    def save(self, path, meta=None):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        np.savez_compressed(path, y=self.y.astype(np.float32), sr=self.sr, t=self.t,
                            meta=json.dumps(meta or {}))


class HardwareRig:
    """OUT and AUX on two inputs of the audio interface; the oC's FX Rig app for the CVs."""

    virtual = False

    def __init__(self, cfg):
        import sounddevice as sd
        from oc import OcCv
        self.sd = sd
        a = cfg["audio"]
        self.sr = a.get("sr", 48000)
        names = [d["name"] for d in sd.query_devices()]
        hits = [i for i, n in enumerate(names) if a["in_device"] in n]
        if not hits:
            raise RuntimeError(f"audio device '{a['in_device']}' not found; have {names}")
        self.in_dev = hits[0]
        self.in_ch = a["in"]
        self.update_out = a.get("update_out")
        self.overflows = 0
        self.oc = OcCv(roles=cfg["oc"], bounds=tuple(cfg.get("cv_bounds", (-5, 8))))
        if not self.oc.ensure_active():
            raise RuntimeError("could not select the FX Rig app on the oC")
        self.t0 = time.monotonic()

    def now(self):
        return time.monotonic() - self.t0

    def sleep(self, s):
        time.sleep(s)

    def capture(self, seconds, tries=3):
        """Records OUT and AUX. A recording which does not end in time (PortAudio stalls now
        and then) is stopped and made again."""
        for attempt in range(tries):
            t = self.now()
            rec = self.sd.rec(int(seconds * self.sr), samplerate=self.sr,
                              channels=max(self.in_ch), device=self.in_dev, dtype="float32")
            deadline = time.monotonic() + seconds + 5.0
            stream = self.sd.get_stream()
            while stream.active and time.monotonic() < deadline:
                time.sleep(0.05)
            if not stream.active:
                self._check_status()
                y = np.stack([rec[:, c - 1] for c in self.in_ch], 1).astype(np.float64)
                return Capture(y, self.sr, t)
            print(f"    WARNING: the recording stalled; again ({attempt + 1})")
            self.sd.stop()
        raise RuntimeError("the audio interface does not record")

    def _check_status(self):
        """Samples lost by the Mac (not the module) would look like dropouts."""
        status = self.sd.get_status()
        if status.input_overflow or status.output_underflow:
            self.overflows += 1
            print(f"    WARNING: the audio interface lost samples ({status})")

    def play_adaptive(self, signal, sr, adapt, target, gain, tail=1.0, monitor=None):
        """Plays signal on the output config["audio"]["update_out"] (into MODEL), recording
        OUT and AUX, and from adapt[0] to adapt[1] seconds, sets the gain so that OUT (where
        the bootloader and the updater copy what they receive) peaks at target. monitor
        (recording so far, number of samples) is called about twice a second, and stops the
        playback (silence follows) when it returns True. Returns the recording and the final
        gain."""
        sd = self.sd
        n = len(signal) + int(tail * sr)
        rec = np.zeros((n, len(self.in_ch)), np.float32)
        out_channels = self.update_out
        state = {"pos": 0, "gain": gain, "status": None, "stop": False}
        signal = np.asarray(signal, np.float32)
        window = int(0.1 * sr)

        def callback(indata, outdata, frames, time_info, status):
            if status:
                state["status"] = status
            i = state["pos"]
            outdata.fill(0)
            chunk = signal[i:i + frames]
            if not state["stop"]:
                outdata[:len(chunk), out_channels - 1] = chunk * state["gain"]
            m = min(frames, n - i)
            if m > 0:
                rec[i:i + m] = indata[:m, [c - 1 for c in self.in_ch]]
            if adapt[0] * sr <= i <= adapt[1] * sr:
                peak = float(np.abs(rec[max(0, i + m - window):i + m, 0]).max())
                step = (target / peak) ** 0.25 if peak > 0.005 else 1.2
                state["gain"] = min(1.0, state["gain"] * float(np.clip(step, 0.85, 1.2)))
            state["pos"] = i + frames
            if state["pos"] >= min(n, state.get("end", n)):
                raise sd.CallbackStop

        t = self.now()
        with sd.Stream(device=self.in_dev, samplerate=sr, blocksize=480, dtype="float32",
                       channels=(max(self.in_ch), out_channels), callback=callback) as stream:
            while stream.active:
                sd.sleep(500)
                if monitor and not state["stop"] and monitor(rec, state["pos"]):
                    state["stop"] = True
                    state["end"] = state["pos"] + sr     # a second of silence, then the end
        if state["status"]:
            self.overflows += 1
            print(f"    WARNING: the audio interface lost samples ({state['status']})")
        rec = rec[:min(n, state.get("end", n))]
        return Capture(rec.astype(np.float64), sr, t), state["gain"]

    def play(self, signal, sr, record=True):
        """Plays signal (mono) on the output config["audio"]["update_out"] only (the
        updates, into MODEL), recording OUT and AUX meanwhile."""
        if sr != self.sr:
            raise RuntimeError(f"{sr}Hz: the rig runs at {self.sr}Hz")
        t = self.now()
        out = np.asarray(signal, dtype=np.float32)[:, None]
        rec = self.sd.playrec(out, samplerate=self.sr, device=self.in_dev,
                              input_mapping=self.in_ch, output_mapping=[self.update_out],
                              dtype="float32")
        self.sd.wait()
        self._check_status()
        return Capture(rec.astype(np.float64), self.sr, t)

    def ask(self, text, sim=None):
        print("\n>>> " + text)
        input("    (Enter when done) ")

    def close(self):
        try:
            self.oc.zero()
        finally:
            self.oc.close()


class VirtualOc:
    """The oC of the virtual rig: each role's output reaches the jacks of config.json "jacks"
    (a mult can send one output to several)."""

    def __init__(self, rig, cfg):
        self.rig = rig
        self.jacks = cfg["jacks"]
        self.volts = {}

    def set(self, role, volts):
        self.volts[role] = volts
        for jack in self.jacks.get(role, []):
            if jack not in self.rig.detached:
                self.rig.send(f"cv {jack} {volts:.6f}")

    def glide(self, role, volts, ms):
        """In 20 steps."""
        start = self.volts.get(role, 0.0)
        for i in range(1, 21):
            self.set(role, start + (volts - start) * i / 20)
            self.rig.sleep(ms / 1000 / 20)

    def trigger_train(self, role, rate, seconds, volts=5.0, width_ms=5):
        """As on the oC, the train runs while the rig goes on (during the next waits and
        captures)."""
        steps = []
        for i in range(int(seconds * rate)):
            steps += [(i * 1000.0 / rate, role, volts), (i * 1000.0 / rate + 10, role, 0.0)]
        self.sequence(steps)

    def sequence(self, steps):
        """As on the oC: the steps play while the rig goes on (during the next waits and
        captures). Glides in 20ms steps."""
        self.steps = list(steps)
        t0 = self.rig.t
        events = []
        last = dict(self.volts)
        for step in sorted(steps, key=lambda q: q[0]):
            t_ms, role, volts = step[:3]
            glide = step[3] if len(step) > 3 else 0
            start = last.get(role, 0.0)
            n = max(1, int(glide / 20))
            for i in range(1, n + 1):
                v = volts if glide == 0 else start + (volts - start) * i / n
                for jack in self.jacks.get(role, []):
                    events.append((t0 + (t_ms + glide * i / n) / 1000.0, jack, v))
            last[role] = volts
            self.volts[role] = volts
        self.rig.events = sorted(self.rig.events + events)

    def replay(self):
        self.sequence(self.steps)

    glitch = 0.0    # volts: a spike in the middle of each level (crosstalk, tests)

    def command(self, cmd, args=(), role="debug"):
        """As on the module, where the oC's steps and the module's readings are not
        synchronized: a reading halfway between two levels, for a block, at each change."""
        from oc import command_levels, SYMBOL
        previous = self.volts.get(role, 0.0)
        for v in command_levels(cmd, args):
            if v != previous:
                self.set(role, (v + previous) / 2)
                self.rig.sleep(0.00025)
            self.set(role, v)
            if self.glitch:
                self.rig.sleep(SYMBOL / 2)
                self.set(role, v + self.glitch)
                self.rig.sleep(0.00025)
                self.set(role, v)
                self.rig.sleep(SYMBOL / 2 - 0.00025)
            else:
                self.rig.sleep(SYMBOL)
            previous = v

    def zero(self):
        for role in list(self.jacks):
            self.set(role, 0.0)

    def role_of(self, jack):
        for role, jacks in self.jacks.items():
            if jack in jacks:
                return role
        return None


class VirtualRig:
    virtual = True

    def __init__(self, cfg, hw=(), sim=SIM_RIG):
        if not Path(sim).exists():
            raise RuntimeError(f"{sim} is missing: make -f plaits/test/host.mk")
        self.p = subprocess.Popen([str(sim)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, bufsize=1, cwd=ROOT)
        self.dir = Path(tempfile.mkdtemp(prefix="plaits-rig-"))
        self.t = 0.0
        self.n = 0
        # Jacks not connected to the oC (unplugged, or patched to OUT or AUX).
        self.detached = set(cfg.get("virtual", {}).get("detached", []))
        self.events = []    # (time, jack, volts) of oC sequences in progress
        self.oc = VirtualOc(self, cfg)
        for line in list(cfg.get("virtual", {}).get("typical", [])) + list(hw):
            self.send(line)
        for line in cfg.get("virtual", {}).get("factory", []):
            self.send(line)
        for line in cfg.get("virtual", {}).get("setup", []):
            self.send(line)
        self.oc.zero()
        self.sr = None

    def send(self, line):
        self.p.stdin.write(line + "\n")
        if line.startswith("wait "):
            ms = line.split()[1]
            self.t += float(ms[:-2]) / 1000 if ms.endswith("ms") else float(ms[:-1])
        elif line.startswith("tap "):
            self.t += 0.2
        elif line == "trigger":
            self.t += 0.01

    def now(self):
        return self.t

    def sleep(self, s):
        """Lets the module run; the oC sequences in progress play their steps."""
        end = self.t + s
        while self.events and self.events[0][0] <= end:
            te, jack, volts = self.events.pop(0)
            if te > self.t + 1e-6:
                self.send(f"wait {(te - self.t) * 1000:.2f}ms")
            if jack not in self.detached:
                self.send(f"cv {jack} {volts:.6f}")
        if end > self.t + 1e-6:
            self.send(f"wait {(end - self.t) * 1000:.2f}ms")

    def sync(self):
        self.send("echo SYNC")
        self.p.stdin.flush()
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("plaits-sim-rig stopped")
            if line.strip() == "SYNC":
                return

    def capture(self, seconds):
        t = self.t
        self.n += 1
        path = self.dir / f"capture{self.n}.wav"
        self.send("clear")
        self.sleep(seconds)
        self.send(f"save {path}")
        self.sync()
        sr, y = wavfile.read(path)
        os.remove(path)
        self.sr = sr
        return Capture(y.astype(np.float64) / 32768.0, sr, t)

    def ask(self, text, sim=None):
        for line in sim or []:
            if line.startswith("@replug "):
                jack = line.split()[1]
                self.detached.discard(jack)
                role = self.oc.role_of(jack)
                self.send(f"cv {jack} {self.oc.volts.get(role, 0.0):.6f}")
            else:
                if line.split()[0] in ("unpatch", "patch", "cv"):
                    self.detached.add(line.split()[1])
                self.send(line)
        print("    [virtual] " + text.split("\n")[0])

    def close(self):
        try:
            self.p.stdin.close()
            self.p.wait(timeout=10)
        except Exception:
            self.p.kill()


def load_config():
    return json.loads((ROOT / "rig/config.json").read_text())


def make_rig(cfg, virtual=False, hw=()):
    return VirtualRig(cfg, hw) if virtual else HardwareRig(cfg)
