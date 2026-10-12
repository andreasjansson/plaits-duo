"""Controls the rig firmware through its commands on FM (plaits/rig/rig.h), sent by the oC
(role debug), and updates it through its updater (plaits/rig/updater).

    ctl = Control(rig)
    ctl.telemetry(0)                     # no telemetry on AUX until asked
    ctl.engine(6, "split")               # the string model, split layout
    ctl.knob("harmonics", 0.5)           # exactly; None: the knob again
    ctl.cv("v_oct", reading=-0.2)        # V/OCT reads -0.2; None: the input again
    ctl.patched(trigger=True)            # TRIGGER seen as patched; None: detected
    f = ctl.frame()                      # a frame now (telemetry.Frame)
    ctl.instrument("sine", 1000, 8000, 8000)
    flash(rig, "build/plaits_rig/plaits_rig.wav")

A command takes about 0.1s. frame() checks that the module counted the commands sent since
the previous frame.
"""
import time
from pathlib import Path

import numpy as np

import telemetry

BUTTONS, ENGINE, KNOB, CV, PATCHED, TELEMETRY, RESET, INSTRUMENT, UPDATE = range(1, 10)
INSTRUMENTS = {"off": 0, "sine": 1, "constant": 2, "readings": 3, "burn": 4}
DEFAULT_INTERVAL = 8000
UPDATER_ADDRESS, START_ADDRESS = 0x0803C000, 0x08008000


def image_hash(path):
    """FNV-1a of a firmware image's 32-bit words, as the rig firmware computes it."""
    data = Path(path).read_bytes()
    data += b"\xff" * (-len(data) % 4)
    h = 2166136261
    for w in np.frombuffer(data, "<u4"):
        h = ((h ^ int(w)) * 16777619) & 0xFFFFFFFF
    return h


class Control:
    def __init__(self, rig):
        self.rig = rig
        self.commands = None     # executed, as the last frame said
        self.sent = 0            # since
        self.interval = DEFAULT_INTERVAL

    def send(self, cmd, *args):
        self.rig.oc.command(cmd, args)
        self.sent += 1

    def buttons(self, which, ms=100):
        """Presses "left", "right" or "both" for ms (None: until buttons("none"))."""
        mask = {"none": 0, "left": 1, "right": 2, "both": 3}[which]
        self.send(BUTTONS, mask, max(1, min(255, round(ms / 4))) if ms else 0)

    def engine(self, model, layout=None, settle=0.05):
        """model 0-7, "stereo" or "split" (not saved, nor the MODEL CV added); None: the
        model of the buttons and the MODEL CV again. Then waits a little: a model's reset
        can take the processor for a few blocks."""
        if model is None:
            self.send(ENGINE, 16)
        else:
            self.send(ENGINE, (model & 7) | (8 if layout == "split" else 0))
        self.rig.sleep(settle)

    def knob(self, name, value):
        """value 0-1, None: the knob again (names: telemetry.POTS)."""
        v = 0xFFFF if value is None else min(0xFFFE, max(0, int(round(value * 65535))))
        self.send(KNOB, telemetry.POTS.index(name), v >> 8, v & 0xFF)

    def cv(self, name, reading=None):
        """The reading (-1 to 1) of a CV input, None: the input again."""
        r = 0 if reading is None else int(round(np.clip(reading, -1, 32767 / 32768) * 32768))
        r &= 0xFFFF
        self.send(CV, telemetry.CV_CHANNELS.index(name), r >> 8, r & 0xFF,
                  0 if reading is None else 1)

    def patched(self, **states):
        """The inputs seen as patched (True) or not (False); the others: detected."""
        mask = state = 0
        for name, value in states.items():
            if value is not None:
                bit = 1 << telemetry.PATCHED.index(name)
                mask |= bit
                state |= bit if value else 0
        self.send(PATCHED, mask, state)

    def telemetry(self, interval=DEFAULT_INTERVAL, frame_in=0.0):
        """Frames every interval blocks (0: none), and one in frame_in seconds (0: none)."""
        self.interval = interval
        self.send(TELEMETRY, interval >> 8, interval & 0xFF,
                  min(255, round(frame_in / 0.004)) if frame_in else 0)

    def reset(self, freeze=False):
        """Clears the statistics (the most cycles and the overruns of each model, and of the
        sections of the voice); freeze: keeps the sections' as they are, until the next
        reset (to read them with another model)."""
        if freeze:
            self.send(RESET, 1)
        else:
            self.send(RESET)

    def instrument(self, name, a=0, b=0, c=0):
        """The debug instrument (plaits/rig/rig.h) on OUT and AUX: "off", "sine" (Hz, OUT
        amplitude, AUX amplitude), "constant" (OUT sample, AUX sample), "readings" (CV
        channel on OUT, on AUX), "burn" (cycles a block lasts at least)."""
        args = [INSTRUMENTS[name]]
        for v in (a, b, c):
            v = int(v) & 0xFFFF
            args += [v >> 8, v & 0xFF]
        self.send(INSTRUMENT, *args)

    def frame(self, delay=0.6, check=True, tries=3, light=None):
        """The module's state now: a frame requested in delay seconds (again if it does not
        come, or is not readable: the audio interface can take 0.3s to start recording).
        light (model, layout): selected before each request (a model which overruns garbles
        the frames)."""
        length = telemetry.BITS * telemetry.SAMPLES_PER_BIT / telemetry.MODULE_RATE
        for attempt in range(tries):
            if light is not None:
                self.engine(*light)
                # A model's reset can take the processor for a few blocks (the vowel
                # model: 8), long enough to lose a symbol of the next command.
                self.rig.sleep(0.1)
            self.telemetry(self.interval, frame_in=delay)
            cap = self.rig.capture(delay + length + 0.25)
            frames = [f for f in telemetry.decode(cap.aux, cap.sr) if f.version >= 2]
            if frames:
                break
            self.failed = cap
        else:
            raise RuntimeError("no telemetry frame: is the rig firmware (version 2 or later) running, "
                               "and oC jack H patched into FM?")
        f = frames[-1]
        if check and self.commands is not None and f.commands != self.commands + self.sent:
            raise RuntimeError(f"the module executed {f.commands - self.commands} of the "
                               f"{self.sent} commands sent ({f.command_errors} errors)")
        self.commands, self.sent = f.commands, 0
        return f

    def update(self):
        """Starts the updater."""
        self.send(UPDATE, 0x55, 0xAA)

    def restart(self):
        """Restarts the module (as at power on, through the bootloader)."""
        self.send(UPDATE, 0x5A, 0xA5)


def updater_status(rig, seconds=0.5):
    cap = rig.capture(seconds)
    frames = telemetry.decode(cap.aux, cap.sr, kind="updater")
    return frames[-1] if frames else None


# The level of the update signal, as OUT copies it (the bootloader's and the updater's
# gain applied): about the middle of their level meter (16000 of 32768), in the units of the
# recording (OUT, 12000 DAC units recorded as 0.39).
TARGET_ECHO = 0.35
# The level of the update file into the updater (dBFS on the audio interface's output 2 of
# the rig, 2026-10-12: the updater measures a peak of 13856 of 32768).
UPDATER_LEVEL = -3.0


def stop_on_error(say=print):
    """A monitor for play_adaptive: decodes the updater's state in the last 2s, and stops
    the update at its first error (so that the updater does not start again in the middle
    of it)."""
    seen = {"errors": None, "last": None}

    def monitor(rec, n):
        sr = 48000
        a = max(0, n - 2 * sr)
        frames = telemetry.decode(rec[a:n, 1].astype(np.float64), sr, kind="updater")
        if not frames:
            return False
        f = frames[-1]
        if seen["errors"] is None:
            seen["errors"] = f.errors
        if (f.state, f.packets // 64) != seen["last"]:
            seen["last"] = (f.state, f.packets // 64)
            say(f"  {n / sr:6.1f}s {f.summary()}")
        if f.errors > seen["errors"] or f.state == 2:
            say(f"  {n / sr:6.1f}s error: stopping ({f.summary()})")
            return True
        return False
    return monitor


def play_update(rig, wav, say=print, intro=4.0, monitor=None, gain=None):
    """Plays an update file into MODEL, its carrier (between the first second of silence and
    the first packet) lengthened to intro seconds, during which the level is set. Returns the
    recording."""
    import scipy.io.wavfile as wavfile
    sr, signal = wavfile.read(wav)
    signal = signal.astype(np.float64) / 32768.0
    blank = signal[sr:2 * sr]     # 6000 cycles of the 6kHz carrier: repeats seamlessly
    extra = int(round(intro)) - 1
    signal = np.concatenate([signal[:sr]] + [blank] * extra + [signal[sr:]])
    say(f"playing {Path(wav).name} ({len(signal) / sr:.0f}s) on output {rig.update_out}")
    # A fixed gain (in dB), or one set during the carrier.
    adapt = (1.15, 1.0 + intro - 0.3) if gain is None else (1e9, 1e9)
    start = 10 ** ((-12 if gain is None else gain) / 20)
    cap, gain = rig.play_adaptive(signal, sr, adapt=adapt, target=TARGET_ECHO, gain=start,
                                  monitor=monitor)
    packets = cap.out[int((1.0 + intro + 0.2) * sr):int((1.0 + intro + 5.0) * sr)]
    if packets.size:
        say(f"level {20 * np.log10(gain):+.1f}dBFS: OUT peaks at {np.abs(packets).max():.2f} "
            f"(aim {TARGET_ECHO})")
    return cap


def running_image(rig, wait=3.0, tries=4):
    """The frame of the firmware which runs (after an update: it starts in about 2s)."""
    time.sleep(wait)
    for _ in range(tries):
        try:
            return Control(rig).frame(check=False)
        except RuntimeError:
            time.sleep(1.0)
    return None


def install(rig, wav, say=print):
    """Installs the rig firmware and its updater with the bootloader (the module started with
    the left button held): plays plaits_rig_install.wav."""
    expected = image_hash(Path(wav).parent / "plaits_rig.bin")
    play_update(rig, wav, say)
    f = running_image(rig)
    if f is None or f.image != expected:
        raise RuntimeError(f"the module does not run the rig firmware {expected:08x}: "
                           f"{f.summary() if f else 'no telemetry'}")
    say(f"installed: {f.summary()}")
    return f


def flash(rig, wav, say=print, attempts=3):
    """Updates the rig firmware with its updater: the update file is played on the audio
    interface's output config["audio"]["update_out"], into MODEL. Returns the frame of the
    new firmware."""
    wav = Path(wav)
    if wav.with_suffix(".bin").stat().st_size > UPDATER_ADDRESS - START_ADDRESS:
        raise RuntimeError(f"{wav} is too large for the updater: use the bootloader")
    expected = image_hash(wav.with_suffix(".bin"))
    ctl = Control(rig)
    f = ctl.frame(check=False, light=(7, "split"))
    say(f"running: image {f.image:08x}; flashing {wav.name} (image {expected:08x})")
    ctl.update()
    for attempt in range(attempts):
        for _ in range(20):
            status = updater_status(rig)
            if status and status.state == 0:
                break
            time.sleep(0.5)
        else:
            raise RuntimeError("the updater is not waiting for an update (no state on AUX)")
        say(status.summary())
        # The updater's gain is fixed: a fixed level, the file as it is.
        cap = play_update(rig, wav, say, intro=1.0, gain=UPDATER_LEVEL,
                          monitor=stop_on_error(say))
        updater = telemetry.decode(cap.aux, cap.sr, kind="updater")
        if updater:
            say("last state: " + updater[-1].summary())
        f = running_image(rig)
        if f is not None and f.image == expected:
            say(f"updated: {f.summary()}")
            return f
        say(f"attempt {attempt + 1}: the module does not run the new image")
    raise RuntimeError("the update failed")
