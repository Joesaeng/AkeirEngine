#!/usr/bin/env python3
"""MCP `play` tool (ADR-0041): open a resident world, step it with input, inspect/query/snapshot, close — over stdio.

    python scripts/test_mcp_play.py --preset msvc-headless [--project Game]
"""
import argparse, json, os, pathlib, subprocess, sys, time

ROOT = pathlib.Path(__file__).resolve().parents[1]


class Mcp:
    def __init__(self, exe, project):
        self.p = subprocess.Popen([str(exe), 'mcp', '--project', str(project)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=open(os.devnull, 'wb'), cwd=str(ROOT))
        self.n = 1

    def request(self, method, params=None, timeout=120):
        rid = self.n; self.n += 1
        m = {'jsonrpc': '2.0', 'id': rid, 'method': method}
        if params is not None: m['params'] = params
        self.p.stdin.write((json.dumps(m) + '\n').encode()); self.p.stdin.flush()
        t0 = time.time()
        while time.time() - t0 < timeout:
            line = self.p.stdout.readline()
            if not line: raise RuntimeError('adapter closed stdout')
            try: r = json.loads(line)
            except json.JSONDecodeError: continue
            if r.get('id') == rid: return r
        raise RuntimeError('timeout')

    def play(self, **args):
        r = self.request('tools/call', {'name': 'play', 'arguments': args})
        env = r['result']['structuredContent']
        if not env['ok']: raise RuntimeError(f"play {args.get('action')}: {env['error']['ruleId']} {env['error']['message']['text']}")
        return env['result']

    def close(self):
        self.p.stdin.close(); return self.p.wait(10)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--preset', default='msvc-headless'); ap.add_argument('--project', default='Game'); a = ap.parse_args()
    exe = ROOT / 'build' / a.preset / 'bin' / 'akeir.exe'
    m = Mcp(exe, ROOT / a.project)
    m.request('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {}, 'clientInfo': {'name': 'test', 'version': '0'}})
    tools = {t['name'] for t in m.request('tools/list')['result']['tools']}
    assert 'play' in tools, tools
    opened = m.play(action='open')
    run = opened['run']; h0 = opened['hash']
    print(f"[1] open {run} entities={opened['entities']} hash={h0}")
    s1 = m.play(action='step', run=run, ticks=60, input={'MoveX': 1})
    assert s1['tick'] == 60 and s1['hash'] != h0, s1
    player = m.play(action='inspect', run=run, entity='Player')
    x = player['components']['Transform']['position'][0]
    assert x > 2.0, f'player did not move right: {x}'
    print(f"[2] stepped 60 with MoveX=1 -> player.x={x:.2f} hash={s1['hash']}")
    q = m.play(action='query', run=run, **{'with': ['EnemyAI']})
    assert q['total'] == 3, q
    s2 = m.play(action='step', run=run, ticks=300)
    hp = m.play(action='inspect', run=run, entity='Player')['components']['Health']['current']
    print(f"[3] tick={s2['tick']} enemies={q['total']} player.hp={hp}")
    assert hp < 100, 'goblins should have hit the player by tick 360'
    snap = m.play(action='snapshot', run=run)
    assert snap['snapshot']['tick'] == 360 and len(snap['snapshot']['entities']) == 10, snap['snapshot'].get('tick')
    # a second run from the same seed stepped the same way lands on the same hash (T0 through the resident path)
    r2 = m.play(action='open')['run']
    m.play(action='step', run=r2, ticks=60, input={'MoveX': 1}); s2b = m.play(action='step', run=r2, ticks=300)
    assert s2b['hash'] == s2['hash'], (s2b['hash'], s2['hash'])
    print(f"[4] second run reproduces hash {s2['hash']}")
    assert len(m.play(action='list')['runs']) == 2
    # ADR-0045: pointer input is accepted by step (window pixels + buttons); edges derive from the previous step
    s3 = m.play(action='step', run=r2, ticks=2, input={'Attack': 1, 'pointer': {'x': 640, 'y': 360, 'buttons': ['left']}})
    assert s3['tick'] == 362, s3
    print("[4b] pointer input step accepted")
    m.play(action='close', run=run); m.play(action='close', run=r2)
    assert len(m.play(action='list')['runs']) == 0
    print('[5] closed; adapter exit', m.close())
    print('OK')


if __name__ == '__main__':
    main()
