#!/usr/bin/env python3
"""The Plaits Duo rig (docs/device-debugging-workflow.md).

    rig.py devices                      the audio interface and the oC
    rig.py check                        the module's state and the wiring
    rig.py levelcal floor               experiments, in order (see rig/host/experiments.py)
    rig.py session                      all of them, guided
    rig.py --virtual session            the same against plaits-sim-rig, no hardware
    rig.py --virtual --hw "gain_error level 1.02" --hw "hw vbat_ratio 1.003" levelcal
    rig.py decode capture.npz           the telemetry frames of a saved capture
    rig.py frame                        the module's state now (rig firmware, version 2 or later)
    rig.py ctl engine 6 split           a command (rig/host/control.py: Control's methods)
    rig.py flash [UPDATE.wav]           updates the rig firmware with its updater (default:
                                        build/plaits_rig/plaits_rig.wav)
    rig.py install                      the rig firmware and its updater, with the bootloader
                                        (build/plaits_rig/plaits_rig_install.wav)

Each run writes rig/results/<name>-<date>/: a JSON file per experiment, the raw captures
(.npz), and log.txt.
"""
import argparse
import json
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "host"))

import experiments  # noqa: E402
import rigio  # noqa: E402
import telemetry  # noqa: E402


def devices(cfg):
    import sounddevice as sd
    names = [d["name"] for d in sd.query_devices()]
    found = [n for n in names if cfg["audio"]["in_device"] in n]
    print("audio interface:", found[0] if found else f"NOT FOUND (have {names})")
    try:
        from oc import OcCv
        oc = OcCv(roles=cfg["oc"])
        print("oC: FX Rig", "active" if oc.ensure_active() else "NOT ACTIVE", oc.query())
        oc.close()
    except Exception as e:
        print("oC:", e)


def decode(path):
    import numpy as np
    d = np.load(path)
    frames, rate = telemetry.decode(d["y"][:, 1], float(d["sr"]), return_rate=True)
    for f in frames:
        print(f"{f.t:8.3f}s margin {f.margin:.2f}{' inverted' if f.inverted else ''}: {f.summary()}")
    if rate:
        print(f"module sample rate: {rate:.2f}Hz")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--virtual", action="store_true", help="plaits-sim-rig instead of the module")
    ap.add_argument("--hw", action="append", default=[],
                    help="virtual: a plaits-sim command at power on (hardware errors)")
    ap.add_argument("--name", help="results folder name (default: the experiments)")
    ap.add_argument("what", nargs="+")
    args = ap.parse_args()
    cfg = rigio.load_config()
    if args.what[0] == "devices":
        return devices(cfg)
    if args.what[0] == "decode":
        return decode(args.what[1])
    if args.what[0] in ("frame", "ctl", "flash", "install"):
        import control
        rig = rigio.make_rig(cfg, virtual=args.virtual, hw=args.hw)
        try:
            if args.what[0] == "install":
                control.install(rig, HERE.parent / "build/plaits_rig/plaits_rig_install.wav")
                return
            if args.what[0] == "flash":
                wav = args.what[1] if len(args.what) > 1 else \
                    HERE.parent / "build/plaits_rig/plaits_rig.wav"
                control.flash(rig, wav)
            elif args.what[0] == "ctl":
                ctl = control.Control(rig)
                values = []
                for a in args.what[2:]:
                    try:
                        values.append(float(a) if "." in a else int(a))
                    except ValueError:
                        values.append(None if a == "none" else a)
                getattr(ctl, args.what[1])(*values)
            print(control.Control(rig).frame(check=False).summary())
        finally:
            rig.close()
        return
    steps = []
    for w in args.what:
        if w == "session":
            steps += experiments.SESSION
        elif w in experiments.EXPERIMENTS:
            steps.append(w)
        else:
            ap.error(f"unknown experiment {w}: {', '.join(experiments.EXPERIMENTS)}")
    name = args.name or ("virtual-" if args.virtual else "") + "-".join(
        dict.fromkeys(s if isinstance(s, str) else s[0] for s in steps))
    out = HERE / "results" / f"{name}-{time.strftime('%Y%m%d-%H%M%S')}"
    rig = rigio.make_rig(cfg, virtual=args.virtual, hw=args.hw)
    session = experiments.Session(rig, cfg, out)
    if args.virtual and args.hw:
        session.say("virtual hardware: " + "; ".join(args.hw))
    summary = {}
    try:
        for step in steps:
            exp, label = (step, step) if isinstance(step, str) else step
            session.say(f"--- {label}")
            result = experiments.EXPERIMENTS[exp](session)
            session.save(label, result)
            summary[label] = result
    finally:
        rig.close()
        (out / "log.txt").write_text("\n".join(session.log) + "\n")
        print(f"results: {out}")


if __name__ == "__main__":
    main()
