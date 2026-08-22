#!/usr/bin/env python3
"""Resident `akeir mcp` vs. rebuild (ADR-0034) — end-to-end check, used by CI and by hand.

    python scripts/test_resident_rebuild.py --preset msvc-headless [--project Game]

1. starts `akeir mcp --project Game` (the adapter) with stdio pipes, does the MCP handshake, calls the
   `capabilities` tool and records info.exe.sha256 of the worker;
2. touches Game/Source/GameSystems.cpp and rebuilds the `akeir` target WHILE the adapter keeps running —
   before ADR-0034 this failed with LNK1168 (exe locked by the resident process);
3. calls `capabilities` again: the worker must now run the rebuilt akeir.exe (sha256 changed and equal to
   the file on disk) and the response must carry the MCP_WORKER_RESTARTED note; `run` must still work;
4. closes stdin: the adapter and its worker must exit promptly.
Exit code 0 = all good; anything else = failure (message on stderr).
"""
import argparse, hashlib, json, os, pathlib, subprocess, sys, time

ROOT = pathlib.Path(__file__).resolve().parents[1]


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 16), b''):
            h.update(chunk)
    return 'sha256:' + h.hexdigest()


class Mcp:
    def __init__(self, exe, project):
        self.p = subprocess.Popen([str(exe), 'mcp', '--project', str(project)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=open(os.devnull, 'wb'), cwd=str(ROOT))
        self.next_id = 1

    def request(self, method, params=None, timeout=120):
        rid = self.next_id; self.next_id += 1
        msg = {'jsonrpc': '2.0', 'id': rid, 'method': method}
        if params is not None: msg['params'] = params
        self.p.stdin.write((json.dumps(msg) + '\n').encode()); self.p.stdin.flush()
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.p.stdout.readline()
            if not line: raise RuntimeError(f'adapter closed stdout while waiting for {method}')
            try: resp = json.loads(line)
            except json.JSONDecodeError: continue
            if resp.get('id') == rid: return resp
        raise RuntimeError(f'timeout waiting for {method}')

    def notify(self, method, params=None):
        msg = {'jsonrpc': '2.0', 'method': method}
        if params is not None: msg['params'] = params
        self.p.stdin.write((json.dumps(msg) + '\n').encode()); self.p.stdin.flush()

    def call(self, tool, args=None):
        r = self.request('tools/call', {'name': tool, 'arguments': args or {}})
        if 'error' in r: raise RuntimeError(f'{tool}: {r["error"]}')
        return r['result']['structuredContent']

    def close(self, timeout=10):
        self.p.stdin.close()
        try: return self.p.wait(timeout)
        except subprocess.TimeoutExpired:
            self.p.kill(); raise RuntimeError('adapter did not exit after stdin EOF')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--preset', default='msvc-headless')
    ap.add_argument('--project', default='Game')
    a = ap.parse_args()
    exe = ROOT / 'build' / a.preset / 'bin' / 'akeir.exe'
    project = ROOT / a.project
    if not exe.exists(): sys.exit(f'missing {exe} — build first')

    m = Mcp(exe, project)
    init = m.request('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {}, 'clientInfo': {'name': 'test', 'version': '0'}})
    assert 'result' in init, init
    m.notify('notifications/initialized')
    tools = m.request('tools/list')['result']['tools']
    print(f'[1] adapter up, {len(tools)} tools')
    assert len(tools) in (15, 16), tools
    caps = m.call('capabilities')
    assert caps['ok'], caps
    sha_before = caps['result']['info']['exe']['sha256']
    assert sha_before == sha256(exe), 'worker sha256 must equal the file it was started from'
    print(f'[1] worker exe {sha_before[:19]}…')

    # 2. rebuild while the adapter (and its worker) keep akeir.exe open
    src = ROOT / 'Game' / 'Source' / 'GameSystems.cpp'
    os.utime(src, None)   # touch → recompile + relink (the PE timestamp changes even when the code does not)
    t0 = time.time()
    # scripts/build.cmd sets up the MSVC environment (vswhere + vcvars64) — works on a bare shell and on CI alike
    cmd = [str(ROOT / 'scripts' / 'build.cmd'), a.preset, 'build', 'akeir'] if os.name == 'nt' else ['cmake', '--build', '--preset', a.preset, '--target', 'akeir']
    build = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True, encoding='utf-8', errors='replace')
    print(f'[2] rebuild while resident: exit={build.returncode} in {time.time() - t0:.1f}s')
    if build.returncode != 0:
        sys.stderr.write(build.stdout[-3000:] + build.stderr[-3000:])
        sys.exit('rebuild failed while akeir mcp was running (LNK1168?)')
    assert 'LNK1168' not in build.stdout + build.stderr
    sha_disk = sha256(exe)
    assert sha_disk != sha_before, 'the rebuild produced a byte-identical akeir.exe — cannot verify the restart'

    # 3. the next call must be served by the new build
    caps2 = m.call('capabilities')
    sha_after = caps2['result']['info']['exe']['sha256']
    notes = [w.get('ruleId') for w in caps2.get('warnings', [])]
    print(f'[3] worker exe after rebuild {sha_after[:19]}… notes={notes}')
    assert sha_after == sha_disk, f'worker still runs the old build: {sha_after} != {sha_disk}'
    assert 'MCP_WORKER_RESTARTED' in notes, 'first response after the restart must carry MCP_WORKER_RESTARTED'
    run = m.call('run', {'ticks': 60})
    assert run['ok'], run
    print(f'[3] run after restart ok, finalHash={run["result"]["finalHash"]}')
    caps3 = m.call('capabilities')
    assert 'MCP_WORKER_RESTARTED' not in [w.get('ruleId') for w in caps3.get('warnings', [])], 'the note must appear once'

    # 4. shutdown
    code = m.close()
    print(f'[4] adapter exited with {code}')
    stale = list(exe.parent.glob('akeir.exe.stale-*'))
    print(f'[4] stale copies left for the next build to sweep: {len(stale)}')
    print('OK')


if __name__ == '__main__':
    main()
