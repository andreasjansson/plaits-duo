"""Drives the CV outputs of the Ornate Criminal (oC), running fxaid's FX Rig app.

    oc = OcCv(roles={"pitch": "A", ...})
    oc.set("pitch", 2.5)            # volts, by role (config.json "oc") or output letter
    oc.glide("harmonics", 5.0, 2000)
    oc.trigger_train("trigger", 8.0, 4.0)   # 5V pulses at 8Hz for 4s

The app and its serial protocol are fxaid's (~/projects/fxaid/rig/oc/FxRig.h, built and
flashed with fxaid's rig/oc/build.sh): ASCII commands between '#' and ';', each answered by
one line, through ocpilot's serial console (ocpilot.device.Device). The same app serves both
rigs: nothing on the oC changes for Plaits.
"""
import time

# Commands to the rig firmware, on FM (plaits/rig/rig.h): levels of -2V (between symbols),
# then 2 bits per symbol, and +4V to start a command, 1.2V apart, each held for SYMBOL s
# (5ms: a model which overruns, or resets, can take the processor for a few blocks).
LEVEL_LOWEST = -2.0
LEVEL_SPACING = 1.2
LEVEL_GAP, LEVEL_START = 0, 5
SYMBOL = 0.005


def command_levels(cmd, args):
    """The voltages of a command: start, then the command, the number of arguments, the
    arguments and the check byte, 2 bits at a time (the most significant first), each after
    a gap; the line rests at the gap's level."""
    data = [cmd, len(args)] + [int(a) & 0xFF for a in args]
    data.append((sum(data) & 0xFF) ^ 0xA5)
    levels = [LEVEL_GAP] * 8 + [LEVEL_START]   # the rest for 20ms, then the start
    for b in data:
        for k in (6, 4, 2, 0):
            levels += [LEVEL_GAP, 1 + ((b >> k) & 3)]
    levels.append(LEVEL_GAP)
    return [LEVEL_LOWEST + LEVEL_SPACING * level for level in levels]


def _num(v):
    v = int(round(v))
    return f"N{-v}" if v < 0 else str(v)


class OcCv:
    def __init__(self, roles, port=None, bounds=(-5.0, 8.0)):
        from ocpilot.device import Device
        self.dev = Device(port=port)
        self.roles = roles
        self.volts = {}
        self.log = []
        self.cmd(f"#B{_num(bounds[0] * 1000)}:{_num(bounds[1] * 1000)};")

    def close(self):
        self.dev.close()

    def ch(self, role):
        return "ABCDEFGH".index(self.roles.get(role, role).upper())

    def cmd(self, s, timeout=2.0):
        """Sends a command; a voltage (#S, #G) again if the oC does not answer (setting
        it twice does no harm, unlike a step of a sequence)."""
        tries = 2 if s.startswith(("#S", "#G")) else 1
        for attempt in range(tries):
            self.dev.drain()
            t = time.monotonic()
            self.dev.t.write(s.encode())
            self.dev.t.flush()
            try:
                line = self.dev.expect(r"^FXR,", timeout=timeout)
                break
            except Exception:
                if attempt == tries - 1:
                    raise
        self.log.append((t, s, line))
        if line.startswith("FXR,ERR"):
            raise RuntimeError(f"oC refused {s}: {line}")
        return line

    def ensure_active(self, tries=2):
        """Makes FX Rig the running app (as fxaid's rig does: top of the app menu, two
        down, select)."""
        for _ in range(tries):
            if self.query()["active"]:
                return True
            self.dev.send("M", gap=0.8)
            self.dev.send("<" * 8, gap=0.12)
            self.dev.send(">>", gap=0.3)
            self.dev.send("]", gap=1.2)
        return self.query()["active"]

    def set(self, role, volts):
        self.volts[role] = volts
        return self.cmd(f"#S{self.ch(role)}:{_num(volts * 1000)};")

    def glide(self, role, volts, ms):
        self.volts[role] = volts
        return self.cmd(f"#G{self.ch(role)}:{_num(volts * 1000)}:{int(ms)};")

    def trigger_train(self, role, rate, seconds, volts=5.0, width_ms=5):
        """Pulses at `rate` Hz for `seconds`, as a sequence run by the oC (exact timing)."""
        self.cmd("#K;")
        period = 1000.0 / rate
        n = int(seconds * rate)
        for i in range(n):
            t = i * period
            self.cmd(f"#A{int(t)}:{self.ch(role)}:{_num(volts * 1000)}:0;")
            self.cmd(f"#A{int(t + width_ms)}:{self.ch(role)}:0:0;")
        return self.cmd("#E;")

    def sequence(self, steps):
        """Plays steps [(t_ms, role, volts[, glide_ms])] from now, timed by the oC (at most
        1024). replay() plays them again."""
        self.cmd("#K;")
        for step in sorted(steps, key=lambda q: q[0]):
            t_ms, role, volts = step[:3]
            glide = int(step[3]) if len(step) > 3 else 0
            glide = 2 if glide == 1 else glide          # 1 marks a step, in FX Rig
            self.cmd(f"#A{int(round(t_ms))}:{self.ch(role)}:{_num(volts * 1000)}:{glide};")
            self.volts[role] = volts
        return self.cmd("#E;")

    def replay(self):
        return self.cmd("#E;")

    def command(self, cmd, args=(), role="debug"):
        """A command to the rig firmware on FM (see command_levels), timed by the Mac."""
        for v in command_levels(cmd, args):
            self.set(role, v)
            time.sleep(SYMBOL)

    def zero(self):
        self.volts = {}
        return self.cmd("#Z;")

    def query(self):
        f = self.cmd("#Q;").split(",")
        pos, n = f[4].split("/")
        return {"tick": int(f[2]), "active": f[3] == "1", "seq": (int(pos), int(n)),
                "running": f[5] == "1", "mv": [int(v) for v in f[6:]]}
