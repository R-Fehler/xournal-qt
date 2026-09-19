#!/usr/bin/env python3
"""Summarize an xqt-inkpad JSONL event log against the ADR-0001 checklist.

Usage: analyze_log.py inkpad-quick-*.jsonl [inkpad-widget-*.jsonl ...]
"""
import json
import statistics
import sys
from collections import Counter, defaultdict


def analyze(path):
    env = None
    events = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            o = json.loads(line)
            if o.get("kind") == "environment":
                env = o
            else:
                events.append(o)

    print(f"=== {path}")
    if env:
        print(f"Qt {env['qtVersion']} platform={env['platform']} dispatcher={env['dispatcher']} "
              f"compressHF={env['compressHighFrequency']} screen={env['screen']}")
    types = Counter(e["ev"] for e in events)
    print("event counts:", dict(types.most_common()))

    tablet = [e for e in events if e["ev"].startswith("Tablet")]
    if tablet:
        devs = Counter((e["dev"].get("name"), e["dev"].get("type"), e.get("ptr")) for e in tablet)
        print("tablet devices/pointer types:", dict(devs))
        drawn = [e for e in tablet if e["ev"] in ("TabletMove", "TabletPress") and e.get("btns", 0) & 1]
        hover = [e for e in tablet if e["ev"] == "TabletMove" and not e.get("btns", 0)]
        print(f"  [1] pressed moves: {len(drawn)}  pressure range: "
              f"{min((e['p'] for e in drawn), default=0):.3f}..{max((e['p'] for e in drawn), default=0):.3f}")
        tilts = [(e["xt"], e["yt"]) for e in tablet if "xt" in e]
        if tilts:
            print(f"      tilt x {min(t[0] for t in tilts):.1f}..{max(t[0] for t in tilts):.1f}  "
                  f"y {min(t[1] for t in tilts):.1f}..{max(t[1] for t in tilts):.1f}  "
                  f"rotation seen: {any(abs(e.get('rot', 0)) > 0 for e in tablet)}")
        frac = sum(1 for e in drawn if e["pos"][0] % 1 or e["pos"][1] % 1)
        print(f"      sub-pixel positions: {frac}/{len(drawn)}")
        print(f"  [2] eraser pointer events: {sum(1 for e in tablet if e.get('ptr') == 'Eraser')}")
        print(f"  [3] button masks seen: {sorted(set(e.get('btns', 0) for e in tablet))} "
              f"(1=tip 2=right 4=middle)")
        print(f"  [4] hover moves: {len(hover)}")
        print(f"  [5] proximity events: enter={types.get('TabletEnterProximity', 0)} "
              f"leave={types.get('TabletLeaveProximity', 0)}")
        moves = sorted(e["ts"] for e in tablet if e["ev"] == "TabletMove")
        dts = [b - a for a, b in zip(moves, moves[1:]) if 0 < b - a < 100]
        if dts:
            print(f"      move interval: median {statistics.median(dts):.1f} ms "
                  f"(~{1000 / statistics.median(dts):.0f} Hz)")
        offs = [e["rt"] - e["ts"] for e in tablet if "ts" in e]
        if offs:
            print(f"      receive - event timestamp: median {statistics.median(offs):.1f} ms "
                  f"(plausible if 0..30; otherwise the clocks differ)")
    else:
        print("  no tablet events!")

    mouse = [e for e in events if e["ev"].startswith("Mouse")]
    synth = [e for e in mouse if e.get("src", 0) != 0 or e["dev"].get("type") not in ("Mouse", "TouchPad")]
    print(f"  [6] mouse events: {len(mouse)}, synthesized: {len(synth)} "
          f"(by device: {dict(Counter(e['dev'].get('type') for e in synth))})")

    touch = [e for e in events if e["ev"].startswith("Touch")]
    if touch:
        maxpts = max(len(e["points"]) for e in touch)
        ell = [p["ell"] for e in touch for p in e["points"] if p["ell"][0] > 0]
        print(f"  [7] touch events: {len(touch)}, max simultaneous points: {maxpts}, "
              f"contact ellipse reported: {bool(ell)}")
        tdts = sorted(e["ts"] for e in touch if e["ev"] == "TouchUpdate")
        tdt = [b - a for a, b in zip(tdts, tdts[1:]) if 0 < b - a < 100]
        if tdt:
            print(f"      touch update interval: median {statistics.median(tdt):.1f} ms")
    else:
        print("  [7] no touch events")
    gestures = [e for e in events if e["ev"] == "NativeGesture"]
    print(f"  [8] native gestures: {len(gestures)} types={dict(Counter(e['gesture'] for e in gestures))}")
    wheels = [e for e in events if e["ev"] == "Wheel"]
    print(f"      wheel events: {len(wheels)}")
    print()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    for p in sys.argv[1:]:
        analyze(p)
