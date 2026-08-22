#!/usr/bin/env python3
"""Stress / performance report (ADR-0044): the sample's Stress world (100 goblins) for 18000 ticks, headless, with --profile.

    python scripts/test_perf.py --preset msvc-release [--max-avg-ms 4.0]

Prints avg/max ms per tick, physics and query counters, and checks (1) two runs produce the same finalHash and
(2) avgTickMs stays under --max-avg-ms. The threshold is generous on purpose — shared CI runners are noisy; the
numbers in the log are the real output. Locally a Release build does ~0.3 ms/tick.
"""
import argparse, json, pathlib, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(exe, args):
    r = json.loads(subprocess.run([str(exe)] + args + ['--json'], cwd=str(ROOT / 'Game'), capture_output=True, text=True, encoding='utf-8', errors='replace').stdout)
    if not r['ok']: sys.exit(f"{args[:2]} failed: {r['error']['ruleId']} {r['error']['message']['text']}")
    return r['result']


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--preset', default='msvc-release'); ap.add_argument('--ticks', type=int, default=18000); ap.add_argument('--max-avg-ms', type=float, default=4.0)
    a = ap.parse_args()
    exe = ROOT / 'build' / a.preset / 'bin' / 'akeir.exe'
    ver = run(exe, ['version'])
    print(f"build {ver['buildConfig']} {ver['engineVersion']} exe {ver['exe']['sha256'][:19]}")
    r1 = run(exe, ['run', '--headless', '--world', 'name:Stress', '--ticks', str(a.ticks), '--profile'])
    r2 = run(exe, ['run', '--headless', '--world', 'name:Stress', '--ticks', str(a.ticks)])
    p = r1['profile']
    print(f"stress {a.ticks} ticks: avg {p['avgTickMs']:.3f} ms/tick  max {p['maxTickMs']:.2f}  physics avg {p['physics']['avgMs']:.3f} (bodies {p['physics']['bodies']}, contacts {p['physics']['contactEvents']})  queries/tick {p['queries']['perTick']:.0f}  entities {p['entities']['total']}")
    for s in sorted(p['systems'], key=lambda s: -s['totalMs'])[:5]:
        print(f"  {s['name']:<20} {s['phase']:<12} avg {s['avgMs']:.4f} ms  max {s['maxMs']:.3f}")
    print(f"finalHash {r1['finalHash']}  second run {r2['finalHash']}")
    assert r1['finalHash'] == r2['finalHash'], 'T0 violated on the stress world'
    if ver['buildConfig'] != 'Debug' and p['avgTickMs'] > a.max_avg_ms:
        sys.exit(f"avg tick {p['avgTickMs']:.3f} ms exceeds {a.max_avg_ms} ms")
    print('OK')


if __name__ == '__main__':
    main()
